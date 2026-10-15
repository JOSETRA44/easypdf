#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <thread>
#include <future>
#include <mutex>
#include <map>
#include <regex>
#include <algorithm>
#include <cctype>
#include <cstdlib>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <hpdf.h>

namespace easypdf {

using json = nlohmann::json;
namespace fs = std::filesystem;

/**
 * @brief Cargador de variables de entorno y configuración
 */
class EnvLoader {
public:
    static std::string get(const std::string& key, const std::string& defaultValue = "") {
        static std::mutex mtx;
        std::lock_guard<std::mutex> lock(mtx);
        
        const char* paths[] = { ".env", "../.env", "../../.env" };
        for (const char* path : paths) {
            std::ifstream envFile(path);
            if (!envFile.is_open()) continue;

            std::string line;
            while (std::getline(envFile, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line[0] == '#') continue;

                size_t delimiterPos = line.find('=');
                if (delimiterPos != std::string::npos) {
                    std::string currentKey = line.substr(0, delimiterPos);
                    if (currentKey == key) {
                        return line.substr(delimiterPos + 1);
                    }
                }
            }
        }
        return defaultValue;
    }
};

/**
 * @brief Cliente de Telegram con soporte para HTML y teclados complejos
 */
class TelegramClient {
public:
    explicit TelegramClient(std::string token) : token_(std::move(token)) {}

    json getUpdates(int64_t offset = 0) {
        auto response = cpr::Get(cpr::Url{apiUrl() + "/getUpdates"},
                                 cpr::Parameters{{"offset", std::to_string(offset)}, {"timeout", "30"}},
                                 cpr::Timeout{40000});
        if (response.status_code != 200) return nullptr;
        return json::parse(response.text, nullptr, false);
    }

    void sendMessage(int64_t chatId, const std::string& text, const std::string& parseMode = "HTML") {
        cpr::Post(cpr::Url{apiUrl() + "/sendMessage"},
                  cpr::Payload{{"chat_id", std::to_string(chatId)}, 
                               {"text", text}, 
                               {"parse_mode", parseMode}});
    }

    int64_t sendMessageWithButtons(int64_t chatId, const std::string& text, const json& keyboard, const std::string& parseMode = "HTML") {
        auto response = cpr::Post(cpr::Url{apiUrl() + "/sendMessage"},
                  cpr::Payload{{"chat_id", std::to_string(chatId)}, 
                               {"text", text}, 
                               {"parse_mode", parseMode},
                               {"reply_markup", keyboard.dump()}});
        if (response.status_code != 200) return 0;
        auto result = json::parse(response.text, nullptr, false);
        if (result.is_discarded() || !result.value("ok", false)) return 0;
        return result["result"].value("message_id", int64_t{0});
    }

    void answerCallbackQuery(const std::string& callbackQueryId, const std::string& text = "") {
        cpr::Post(cpr::Url{apiUrl() + "/answerCallbackQuery"},
                  cpr::Payload{{"callback_query_id", callbackQueryId}, {"text", text}});
    }

    bool sendDocument(int64_t chatId, const std::string& filePath, const std::string& caption = "", const std::string& parseMode = "HTML") {
        fs::path p(filePath);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        std::string mimeType = "application/octet-stream";
        if (ext == ".pdf") mimeType = "application/pdf";
        else if (ext == ".docx") mimeType = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        else if (ext == ".doc") mimeType = "application/msword";

        auto response = cpr::Post(cpr::Url{apiUrl() + "/sendDocument"},
                  cpr::Multipart{{"chat_id", std::to_string(chatId)},
                                 {"document", cpr::File{filePath, p.filename().string()}, mimeType},
                                 {"caption", caption},
                                 {"parse_mode", parseMode}});
        if (response.status_code != 200) return false;
        auto result = json::parse(response.text, nullptr, false);
        return !result.is_discarded() && result.value("ok", false);
    }

    void deleteMessage(int64_t chatId, int64_t messageId) {
        cpr::Post(cpr::Url{apiUrl() + "/deleteMessage"},
                  cpr::Payload{{"chat_id", std::to_string(chatId)}, {"message_id", std::to_string(messageId)}});
    }

    std::string downloadFile(const std::string& fileId, const std::string& destPath) {
        auto getFileResponse = cpr::Get(cpr::Url{apiUrl() + "/getFile"},
                                        cpr::Parameters{{"file_id", fileId}});
        if (getFileResponse.status_code != 200) return "";

        auto fileData = json::parse(getFileResponse.text);
        if (!fileData["ok"]) return "";

        std::string filePath = fileData["result"]["file_path"];
        std::string downloadUrl = "https://api.telegram.org/file/bot" + token_ + "/" + filePath;

        std::ofstream file(destPath, std::ios::binary);
        auto downloadResponse = cpr::Get(cpr::Url{downloadUrl}, cpr::Timeout{60000});
        if (downloadResponse.status_code == 200) {
            file << downloadResponse.text;
            if (!file) return "";
            return destPath;
        }
        file.close();
        fs::remove(destPath);
        return "";
    }

private:
    std::string apiUrl() const { return "https://api.telegram.org/bot" + token_; }
    std::string token_;
};

/**
 * @brief Utilidades para limpieza y seguridad de archivos
 */
class FileUtils {
public:
    static std::string escapeHtml(const std::string& value) {
        std::string escaped;
        for (char ch : value) {
            switch (ch) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                case '"': escaped += "&quot;"; break;
                default: escaped += ch;
            }
        }
        return escaped;
    }

    static std::string sanitizeName(std::string name) {
        // Eliminar caracteres ilegales, peligrosos y comillas
        std::regex illegal_chars("[<>:\"/\\\\|?*'%!^]");
        name = std::regex_replace(name, illegal_chars, "_");
        
        // Reemplazar espacios múltiples por uno solo
        std::regex spaces("\\s+");
        name = std::regex_replace(name, spaces, " ");
        
        // Limitar longitud
        if (name.length() > 50) {
            size_t ext_pos = name.find_last_of('.');
            std::string ext = (ext_pos != std::string::npos) ? name.substr(ext_pos) : "";
            name = name.substr(0, 45) + ext;
        }
        while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
        if (name.empty() || name == "." || name == "..") return "archivo.bin";
        return name;
    }

    static std::string formatSize(uintmax_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int i = 0;
        double size = static_cast<double>(bytes);
        while (size > 1024 && i < 3) {
            size /= 1024;
            i++;
        }
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[i]);
        return std::string(buffer);
    }
};

/**
 * @brief Motor de procesamiento avanzado
 */
class FileProcessor {
public:
    static std::string powershellQuote(const std::string& value) {
        std::string quoted = "'";
        for (char ch : value) {
            if (ch == '\'') quoted += "''";
            else quoted += ch;
        }
        return quoted + "'";
    }
    static std::string convertToPdf(const std::string& inputPath) {
        fs::path p(inputPath);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        std::string pdfPath = p.replace_extension(".pdf").string();

        if (ext == ".txt") {
            return txtToPdf(inputPath, pdfPath);
        } else if (ext == ".docx" || ext == ".doc" || ext == ".rtf") {
            return officeToPdf(inputPath, pdfPath);
        } else if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
            return imageToPdf(inputPath, pdfPath);
        }

        return "";
    }

    static std::string convertPdfToDocx(const std::string& inputPath) {
        fs::path p(inputPath);
        std::string docxPath = p.replace_extension(".docx").string();

        std::string absInput = fs::absolute(inputPath).string();
        std::string absOutput = fs::absolute(docxPath).string();

        // Usar Word COM para abrir PDF y guardar como Docx (Word 2013+ soporta esto nativamente)
        std::string cmd = "powershell -NoProfile -Command \"$word = New-Object -ComObject Word.Application; $word.Visible = $false; try { $doc = $word.Documents.Open(" + powershellQuote(absInput) + "); try { $doc.SaveAs([ref]" + powershellQuote(absOutput) + ", [ref]16); } finally { $doc.Close(); } } finally { $word.Quit(); }\"";

        int res = std::system(cmd.c_str());
        if (res == 0 && fs::exists(docxPath)) {
            return docxPath;
        }
        return "";
    }

private:
    static std::string imageToPdf(const std::string& imgPath, const std::string& pdfPath) {
        HPDF_Doc pdf = HPDF_New(NULL, NULL);
        if (!pdf) return "";
        try {
            HPDF_Image image;
            std::string ext = fs::path(imgPath).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".png") {
                image = HPDF_LoadPngImageFromFile(pdf, imgPath.c_str());
            } else {
                image = HPDF_LoadJpegImageFromFile(pdf, imgPath.c_str());
            }

            if (!image) throw std::runtime_error("No se pudo cargar la imagen");

            HPDF_Page page = HPDF_AddPage(pdf);
            float width = HPDF_Image_GetWidth(image);
            float height = HPDF_Image_GetHeight(image);

            HPDF_Page_SetWidth(page, width);
            HPDF_Page_SetHeight(page, height);
            HPDF_Page_DrawImage(page, image, 0, 0, width, height);

            HPDF_SaveToFile(pdf, pdfPath.c_str());
            HPDF_Free(pdf);
            return pdfPath;
        } catch (...) {
            if (pdf) HPDF_Free(pdf);
            return "";
        }
    }

    static std::string txtToPdf(const std::string& txtPath, const std::string& pdfPath) {
        HPDF_Doc pdf = HPDF_New(NULL, NULL);
        if (!pdf) return "";
        try {
            HPDF_Page page = HPDF_AddPage(pdf);
            HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
            HPDF_Font font = HPDF_GetFont(pdf, "Helvetica", NULL);
            HPDF_Page_SetFontAndSize(page, font, 11);

            std::ifstream file(txtPath);
            std::string line;
            float y = HPDF_Page_GetHeight(page) - 40;
            while (std::getline(file, line) && y > 40) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                HPDF_Page_BeginText(page);
                HPDF_Page_TextOut(page, 40, y, line.c_str());
                HPDF_Page_EndText(page);
                y -= 14;
            }
            HPDF_SaveToFile(pdf, pdfPath.c_str());
            HPDF_Free(pdf);
            return pdfPath;
        } catch (...) {
            if (pdf) HPDF_Free(pdf);
            return "";
        }
    }

    static std::string officeToPdf(const std::string& inputPath, const std::string& pdfPath) {
        std::string absInput = fs::absolute(inputPath).string();
        std::string absOutput = fs::absolute(pdfPath).string();
        
        std::string cmd = "powershell -NoProfile -Command \"$word = New-Object -ComObject Word.Application; $word.Visible = $false; try { $doc = $word.Documents.Open(" + powershellQuote(absInput) + "); try { $doc.SaveAs([ref]" + powershellQuote(absOutput) + ", [ref]17); } finally { $doc.Close(); } } finally { $word.Quit(); }\"";
        
        int res = std::system(cmd.c_str());
        if (res == 0 && fs::exists(pdfPath)) {
            return pdfPath;
        }
        return "";
    }
};

} // namespace easypdf
