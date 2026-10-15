#include "easypdf.h"
#include <array>
#include <condition_variable>
#include <deque>

using namespace easypdf;
using namespace std;

// Estado persistente en memoria por sesión de usuario
struct UserState {
    string filePath;
    string originalName;
    int64_t lastMenuMessageId = 0;
};

map<int64_t, UserState> sessions;
mutex stateMtx;

struct UpdateQueue {
    mutex mtx;
    condition_variable ready;
    condition_variable space;
    deque<json> updates;
};

int64_t updateChatId(const json& update) {
    if (update.contains("message")) return update["message"]["chat"].value("id", int64_t{0});
    if (update.contains("callback_query") && update["callback_query"].contains("message"))
        return update["callback_query"]["message"]["chat"].value("id", int64_t{0});
    return 0;
}

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
                fileSize = doc.value("file_size", uintmax_t{0});
            } else {
                // Es una foto (Telegram envía varias resoluciones, tomamos la mayor)
                auto& photos = message["photo"];
                auto& best = photos.back();
                fileId = best["file_id"];
                fileSize = best.value("file_size", uintmax_t{0});
                rawName = "imagen_" + to_string(messageId) + ".jpg";
            }

            string cleanName = FileUtils::sanitizeName(rawName);
            string ext = fs::path(cleanName).extension().string();
            transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
            if (ext != ".pdf" && ext != ".txt" && ext != ".docx" && ext != ".doc" &&
                ext != ".rtf" && ext != ".jpg" && ext != ".jpeg" && ext != ".png") {
                client.sendMessage(chatId, "Formato no compatible.");
                return;
            }
            constexpr uintmax_t maxFileSize = 20 * 1024 * 1024;
            if (fileSize > maxFileSize) {
                client.sendMessage(chatId, "El archivo supera el límite de 20 MB.");
                return;
            }
            string userDir = "temp_" + to_string(chatId);
            fs::create_directories(userDir);
            string tempPath = userDir + "/" + cleanName;

            {
                lock_guard<mutex> lock(stateMtx);
                auto previous = sessions.find(chatId);
                if (previous != sessions.end()) {
                    fs::remove(previous->second.filePath);
                    sessions.erase(previous);
                }
            }

            if (client.downloadFile(fileId, tempPath) != "") {
                if (fs::file_size(tempPath) > maxFileSize) {
                    fs::remove(tempPath);
                    client.sendMessage(chatId, "El archivo supera el límite de 20 MB.");
                    return;
                }
                string info = "<b>📥 Archivo Recibido</b>\n"
                              "📂 <code>" + FileUtils::escapeHtml(cleanName) + "</code>\n"
                              "⚖️ <code>" + FileUtils::formatSize(fileSize) + "</code>\n\n"
                              "¿Qué acción deseas ejecutar?";

                json keyboard;
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

                int64_t menuId = client.sendMessageWithButtons(chatId, info, keyboard);
                if (menuId != 0) {
                    lock_guard<mutex> lock(stateMtx);
                    sessions[chatId] = {tempPath, cleanName, menuId};
                }
                else fs::remove(tempPath);
            } else {
                fs::remove(tempPath);
                client.sendMessage(chatId, "No se pudo descargar el archivo.");
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

        if (state.filePath.empty() || state.lastMenuMessageId != msgId) {
            client.answerCallbackQuery(cbId, "⚠️ Sesión expirada o archivo no encontrado.");
            return;
        }

        if (data == "to_pdf") {
            client.answerCallbackQuery(cbId, "⚙️ Procesando...");
            client.sendMessage(chatId, "⏳ <i>Iniciando conversión a PDF...</i>");
            
            string pdfPath = FileProcessor::convertToPdf(state.filePath);
            
            if (!pdfPath.empty()) {
                if (!client.sendDocument(chatId, pdfPath, "✅ <b>¡Listo!</b> Aquí tienes tu PDF."))
                    client.sendMessage(chatId, "No se pudo enviar el PDF. Inténtalo de nuevo.");
                fs::remove(pdfPath);
            } else {
                client.sendMessage(chatId, "❌ <b>Error:</b> No se pudo convertir este archivo.");
            }
        } else if (data == "to_word") {
            client.answerCallbackQuery(cbId, "⚙️ Procesando...");
            client.sendMessage(chatId, "⏳ <i>Iniciando conversión a Word...</i>");
            
            string docxPath = FileProcessor::convertPdfToDocx(state.filePath);
            
            if (!docxPath.empty()) {
                if (!client.sendDocument(chatId, docxPath, "✅ <b>¡Listo!</b> Aquí tienes tu documento Word."))
                    client.sendMessage(chatId, "No se pudo enviar el documento. Inténtalo de nuevo.");
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
            fs::remove_all(fs::path(state.filePath).parent_path());
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
    constexpr size_t workerCount = 4;
    constexpr size_t queueCapacity = 64;
    array<UpdateQueue, workerCount> queues;
    vector<jthread> workers;
    for (size_t i = 0; i < workerCount; ++i) {
        workers.emplace_back([&client, &queues, i]() {
            auto& queue = queues[i];
            while (true) {
                json update;
                {
                    unique_lock<mutex> lock(queue.mtx);
                    queue.ready.wait(lock, [&queue] { return !queue.updates.empty(); });
                    update = std::move(queue.updates.front());
                    queue.updates.pop_front();
                    queue.space.notify_one();
                }
                try { handleUpdate(client, update); }
                catch (const exception& e) { cerr << "Update error: " << e.what() << endl; }
            }
        });
    }

    cout << "--- EasyPDF Elite Engine Started ---" << endl;

    while (true) {
        try {
            json updates = client.getUpdates(lastUpdateId + 1);
            if (updates != nullptr && updates["ok"]) {
                for (auto& update : updates["result"]) {
                    lastUpdateId = update["update_id"];
                    size_t shard = static_cast<uint64_t>(updateChatId(update)) % workerCount;
                    auto& queue = queues[shard];
                    unique_lock<mutex> lock(queue.mtx);
                    queue.space.wait(lock, [&queue] { return queue.updates.size() < queueCapacity; });
                    queue.updates.push_back(update);
                    queue.ready.notify_one();
                }
            }
        } catch (const exception& e) {
            cerr << "Critical Error: " << e.what() << endl;
            this_thread::sleep_for(chrono::seconds(5));
        }
    }
    return 0;
}
