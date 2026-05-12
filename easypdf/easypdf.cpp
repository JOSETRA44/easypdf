#include "easypdf.h"

using namespace easypdf;
using namespace std;

// Estado persistente en memoria por sesión de usuario
struct UserState {
    string filePath;
    string originalName;
    int64_t lastMenuMessageId;
};

map<int64_t, UserState> sessions;
mutex stateMtx;

void handleUpdate(TelegramClient& client, const json& update) {
    if (update.contains("message")) {
        auto& message = update["message"];
        int64_t chatId = message["chat"]["id"];
        int64_t messageId = message["message_id"];

        if (message.contains("text")) {
            string text = message["text"];
            if (text == "/start") {
                string welcome = "<b>🚀 Bienvenido a EasyPDF Elite</b>\n\n"
                                 "Soy tu procesador de documentos avanzado. Envíame cualquier archivo:\n"
                                 "• 📄 <b>Texto:</b> .txt\n"
                                 "• 📝 <b>Office:</b> .docx, .doc, .rtf\n"
                                 "• 🖼️ <b>Imágenes:</b> .jpg, .png\n"
                                 "• 📕 <b>PDF:</b> Para convertir a Word\n\n"
                                 "<i>Los archivos se procesan de forma segura y se eliminan al instante.</i>";
                client.sendMessage(chatId, welcome);
            }
        } else if (message.contains("document") || message.contains("photo")) {
            string fileId;
            string rawName = "archivo.bin";
            uintmax_t fileSize = 0;

            if (message.contains("document")) {
                auto& doc = message["document"];
                fileId = doc["file_id"];
                rawName = doc.contains("file_name") ? doc["file_name"].get<string>() : "documento.bin";
                fileSize = doc["file_size"];
            } else {
                // Es una foto (Telegram envía varias resoluciones, tomamos la mayor)
                auto& photos = message["photo"];
                auto& best = photos.back();
                fileId = best["file_id"];
                fileSize = best["file_size"];
                rawName = "imagen_" + to_string(messageId) + ".jpg";
            }

            string cleanName = FileUtils::sanitizeName(rawName);
            string userDir = "temp_" + to_string(chatId);
            fs::create_directories(userDir);
            string tempPath = userDir + "/" + cleanName;

            if (client.downloadFile(fileId, tempPath) != "") {
                lock_guard<mutex> lock(stateMtx);
                sessions[chatId] = {tempPath, cleanName, 0};

                string info = "<b>📥 Archivo Recibido</b>\n"
                              "📂 <code>" + cleanName + "</code>\n"
                              "⚖️ <code>" + FileUtils::formatSize(fileSize) + "</code>\n\n"
                              "¿Qué acción deseas ejecutar?";

                json keyboard;
                string ext = fs::path(cleanName).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".pdf") {
                    keyboard = {{"inline_keyboard", {
                        {{{"text", "📝 Convertir a Word"}, {"callback_data", "to_word"}}},
                        {{{"text", "🗑️ Descartar"}, {"callback_data", "cancel"}}}
                    }}};
                } else {
                    keyboard = {{"inline_keyboard", {
                        {{{"text", "📕 Convertir a PDF"}, {"callback_data", "to_pdf"}}},
                        {{{"text", "🗑️ Descartar"}, {"callback_data", "cancel"}}}
                    }}};
                }

                client.sendMessageWithButtons(chatId, info, keyboard);
            }
        }
    } else if (update.contains("callback_query")) {
        auto& cb = update["callback_query"];
        string cbId = cb["id"];
        string data = cb["data"];
        int64_t chatId = cb["message"]["chat"]["id"];
        int64_t msgId = cb["message"]["message_id"];

        UserState state;
        {
            lock_guard<mutex> lock(stateMtx);
            if (sessions.count(chatId)) {
                state = sessions[chatId];
            }
        }

        if (state.filePath.empty()) {
            client.answerCallbackQuery(cbId, "⚠️ Sesión expirada o archivo no encontrado.");
            client.deleteMessage(chatId, msgId);
            return;
        }

        if (data == "to_pdf") {
            client.answerCallbackQuery(cbId, "⚙️ Procesando...");
            client.sendMessage(chatId, "⏳ <i>Iniciando conversión a PDF...</i>");
            
            string pdfPath = FileProcessor::convertToPdf(state.filePath);
            
            if (!pdfPath.empty()) {
                client.sendDocument(chatId, pdfPath, "✅ <b>¡Listo!</b> Aquí tienes tu PDF.");
                fs::remove(pdfPath);
            } else {
                client.sendMessage(chatId, "❌ <b>Error:</b> No se pudo convertir este archivo.");
            }
        } else if (data == "to_word") {
            client.answerCallbackQuery(cbId, "⚙️ Procesando...");
            client.sendMessage(chatId, "⏳ <i>Iniciando conversión a Word...</i>");
            
            string docxPath = FileProcessor::convertPdfToDocx(state.filePath);
            
            if (!docxPath.empty()) {
                // Reutilizamos sendDocument pero Telegram lo tratará como docx por la extensión
                // Sin embargo, nuestro sendDocument tiene application/pdf hardcoded.
                // Deberíamos hacerlo dinámico o crear sendGenericDocument.
                client.sendDocument(chatId, docxPath, "✅ <b>¡Listo!</b> Aquí tienes tu documento Word.");
                fs::remove(docxPath);
            } else {
                client.sendMessage(chatId, "❌ <b>Error:</b> No se pudo convertir el PDF a Word.");
            }
        } else if (data == "cancel") {
            client.answerCallbackQuery(cbId, "🗑️ Archivo eliminado.");
        }

        // Limpieza final
        {
            lock_guard<mutex> lock(stateMtx);
            if (fs::exists(state.filePath)) fs::remove(state.filePath);
            fs::remove_all(fs::path(state.filePath).parent_path()); // Borrar carpeta del usuario
            sessions.erase(chatId);
        }
        client.deleteMessage(chatId, msgId);
    }
}

int main() {
    string token = EnvLoader::get("TELEGRAM_BOT_TOKEN");
    if (token == "" || token == "TU_TOKEN_AQUI") {
        cerr << "ERROR: Token no encontrado en .env" << endl;
        return 1;
    }

    TelegramClient client(token);
    int64_t lastUpdateId = 0;

    cout << "--- EasyPDF Elite Engine Started ---" << endl;

    while (true) {
        try {
            json updates = client.getUpdates(lastUpdateId + 1);
            if (updates != nullptr && updates["ok"]) {
                for (auto& update : updates["result"]) {
                    lastUpdateId = update["update_id"];
                    std::jthread([&client, update]() {
                        handleUpdate(client, update);
                    }).detach();
                }
            }
        } catch (const exception& e) {
            cerr << "Critical Error: " << e.what() << endl;
            this_thread::sleep_for(chrono::seconds(5));
        }
    }
    return 0;
}
