# EasyPDF Elite

EasyPDF Elite is a C++ based Telegram bot designed for advanced document processing and conversion. It supports multiple formats, including text files, Office documents, images, and reverse conversion from PDF to Word.

## Features

- Text to PDF conversion: Converts plain text (.txt) files to PDF format.
- Office to PDF conversion: Supports .docx, .doc, and .rtf files using Microsoft Word COM integration.
- Image to PDF conversion: Converts .jpg, .jpeg, and .png files into PDF documents.
- PDF to Word conversion: Performs reverse conversion from PDF to .docx format.
- Automated Cleanup: All processed files and temporary user directories are deleted immediately after completion.
- Interactive Interface: Uses Telegram inline keyboards for a streamlined user experience.

## Prerequisites

- Windows Operating System (Required for Microsoft Word COM integration).
- Microsoft Word installed (For Office and PDF-to-Word conversions).
- CMake 3.12 or higher.
- C++20 compliant compiler (e.g., MSVC, GCC 13+).
- vcpkg or manual installation of dependencies (CPR, nlohmann/json, libharu).

## Dependencies

The project automatically fetches or links the following libraries:
- CPR: For HTTP requests to the Telegram Bot API.
- nlohmann/json: For JSON parsing and manipulation.
- libharu: For PDF generation and image embedding.

## Installation and Build

1. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/easypdf.git
   cd easypdf
   ```

2. Configure the environment:
   Create a `.env` file in the root directory based on `.env.example`:
   ```text
   TELEGRAM_BOT_TOKEN=your_bot_token_here
   ```

3. Build the project using CMake:
   ```bash
   cmake -B build -S .
   cmake --build build --config Release
   ```

4. Run the application:
   ```bash
   ./build/easypdf/easypdf.exe
   ```

## Architecture

- easypdf.cpp: Main entry point and Telegram update handling logic.
- easypdf.h: Contains core classes including:
    - TelegramClient: Manages API communication.
    - FileProcessor: Handles conversion logic using libharu and COM interfaces.
    - FileUtils: Provides file naming and formatting utilities.
    - EnvLoader: Manages configuration and secrets.

## Usage

1. Start the bot by sending the /start command.
2. Upload a supported file (document or image).
3. Select the desired action from the interactive menu provided by the bot.
4. Download the processed file sent by the bot.

## License

This project is licensed under the MIT License. See the LICENSE file for details.
