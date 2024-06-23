#include <iostream>
#include <opencv2/opencv.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>
#include <Windows.h>

using boost::asio::ip::tcp;
using FrameData = std::pair<std::chrono::high_resolution_clock::time_point, cv::Mat>;

std::unordered_map<uint32_t, FrameData> frame_buffer;
uint32_t last_displayed_index = 0;

cv::Mat captureScreen() {
    HWND hwndDesktop = GetDesktopWindow();
    HDC hdcDesktop = GetDC(hwndDesktop);

    HDC hdcMemory = CreateCompatibleDC(hdcDesktop);
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcDesktop, screenWidth, screenHeight);

    SelectObject(hdcMemory, hBitmap);
    BitBlt(hdcMemory, 0, 0, screenWidth, screenHeight, hdcDesktop, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi = { 0 };
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = screenWidth;
    bi.biHeight = -screenHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    cv::Mat mat(screenHeight, screenWidth, CV_8UC3);
    GetDIBits(hdcMemory, hBitmap, 0, screenHeight, mat.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    DeleteObject(hBitmap);
    DeleteDC(hdcMemory);
    ReleaseDC(hwndDesktop, hdcDesktop);

    return mat;
}

void logTime(const std::string& label, std::chrono::high_resolution_clock::time_point start, std::chrono::high_resolution_clock::time_point end) {
    std::chrono::duration<double, std::milli> duration = end - start;
    std::cout << label << " time: " << duration.count() << " ms\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <0|1> [additional args...]\n";
        return 1;
    }

    int mode = std::stoi(argv[1]);
    if (mode == 1) {  // Sender
        if (argc < 6) {
            std::cerr << "Usage: " << argv[0] << " 1 <receiver IP> <port> <max fps> <window mode>\n";
            return 1;
        }

        const char* host = argv[2];
        int port = std::stoi(argv[3]);
        int fps = std::stoi(argv[4]);
        int window_mode = std::stoi(argv[5]);
        int delay = 1000 / fps;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int targetWidth = screenWidth;
        int targetHeight = screenHeight;

        switch (window_mode) {
        case 1:
            targetWidth = 3840;
            targetHeight = 2160;
            break;
        case 2:
            targetWidth = 1920;
            targetHeight = 1080;
            break;
        case 3:
            targetWidth = 1280;
            targetHeight = 720;
            break;
        case 4:
            targetWidth = 800;
            targetHeight = 600;
            break;
        case 5:
            targetWidth = 640;
            targetHeight = 480;
            break;
        }

        try {
            boost::asio::io_service io_service;
            tcp::socket socket(io_service);
            tcp::resolver resolver(io_service);
            boost::asio::connect(socket, resolver.resolve({ host, std::to_string(port) }));

            cv::Mat frame;
            std::vector<uchar> buffer;
            uint64_t transmitted_bits = 0;
            uint32_t frame_index = 0;
            uint32_t frames_sent = 0;
            auto start_time = std::chrono::steady_clock::now();

            while (true) {
                auto timestamp_before_capture = std::chrono::high_resolution_clock::now();
                int64_t timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp_before_capture.time_since_epoch()).count();

                frame = captureScreen();

                auto timestamp_after_capture = std::chrono::high_resolution_clock::now();
                if (frame.empty()) {
                    std::cerr << "Error: Captured frame is empty.\n";
                    continue;
                }

                if (screenWidth != targetWidth || screenHeight != targetHeight) {
                    cv::resize(frame, frame, cv::Size(targetWidth, targetHeight));
                }

                cv::imencode(".jpg", frame, buffer);

                auto timestamp_before_sending = std::chrono::high_resolution_clock::now();
                uint32_t size = buffer.size();

                boost::asio::write(socket, boost::asio::buffer(&timestamp_ms, sizeof(timestamp_ms)));
                boost::asio::write(socket, boost::asio::buffer(&frame_index, sizeof(frame_index)));
                boost::asio::write(socket, boost::asio::buffer(&size, sizeof(size)));
                boost::asio::write(socket, boost::asio::buffer(buffer.data(), size));

                transmitted_bits += size * 8;
                frame_index++;
                frames_sent++;

                auto timestamp_after_sending = std::chrono::high_resolution_clock::now();

                std::chrono::duration<double, std::milli> capture_duration = timestamp_after_capture - timestamp_before_capture;
                std::chrono::duration<double, std::milli> encode_duration = timestamp_before_sending - timestamp_after_capture;
                std::chrono::duration<double, std::milli> send_duration = timestamp_after_sending - timestamp_before_sending;

                std::this_thread::sleep_for(std::chrono::milliseconds(delay) - (timestamp_after_sending - timestamp_before_capture));

                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= 15) {
                    double avg_fps = frames_sent / 15.0;
                    uint64_t transmitted_megas = transmitted_bits / 8000000;
                    std::cout << "Frames Sent: " << frames_sent << "\n";
                    std::cout << "Average FPS: " << avg_fps << "\n";
                    std::cout << "Total Transmitted Megabytes: " << transmitted_megas << " Megabytes\n";
                    frames_sent = 0;
                    start_time = now;
                }
            }
        }
        catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }
    else if (mode == 0) {  // Receiver
        if (argc < 5) {
            std::cerr << "Usage: " << argv[0] << " 0 <fullscreen> <grid width> <grid height> <port1> [port2] ...\n";
            return 1;
        }

        int fullscreen = std::stoi(argv[2]);
        int grid_width = std::stoi(argv[3]);
        int grid_height = std::stoi(argv[4]);

        std::vector<int> ports;
        for (int i = 5; i < argc; ++i) {
            ports.push_back(std::stoi(argv[i]));
        }

        try {
            boost::asio::io_service io_service;
            std::vector<std::unique_ptr<tcp::acceptor>> acceptors;
            std::vector<std::unique_ptr<tcp::socket>> sockets;
            for (int port : ports) {
                acceptors.push_back(std::make_unique<tcp::acceptor>(io_service, tcp::endpoint(tcp::v4(), port)));
                sockets.push_back(std::make_unique<tcp::socket>(io_service));
                acceptors.back()->accept(*sockets.back());
            }

            uint32_t size;
            std::vector<uchar> buffer;
            cv::Mat frame;
            uint32_t frames_received = 0;
            uint32_t frames_displayed = 0;
            auto start_time = std::chrono::steady_clock::now();
            double total_delay = 0.0;

            while (true) {
                for (auto& socket : sockets) {
                    int64_t timestamp_ms;
                    uint32_t frame_index;

                    auto timestamp_before_receiving = std::chrono::high_resolution_clock::now();

                    boost::asio::read(*socket, boost::asio::buffer(&timestamp_ms, sizeof(timestamp_ms)));
                    boost::asio::read(*socket, boost::asio::buffer(&frame_index, sizeof(frame_index)));
                    boost::asio::read(*socket, boost::asio::buffer(&size, sizeof(size)));
                    buffer.resize(size);
                    boost::asio::read(*socket, boost::asio::buffer(buffer.data(), size));

                    auto timestamp_after_receiving = std::chrono::high_resolution_clock::now();

                    frame = cv::imdecode(buffer, cv::IMREAD_COLOR);

                    if (frame.empty()) {
                        std::cerr << "Error: Decoded frame is empty.\n";
                        continue;
                    }

                    auto timestamp_after_decoding = std::chrono::high_resolution_clock::now();
                    frame_buffer[frame_index] = std::make_pair(std::chrono::high_resolution_clock::time_point(std::chrono::milliseconds(timestamp_ms)), frame);

                    frames_received++;

                    while (!frame_buffer.empty() && frame_buffer.begin()->first <= last_displayed_index) {
                        frame_buffer.erase(frame_buffer.begin());
                    }

                    if (!frame_buffer.empty()) {
                        auto display_frame_index = frame_buffer.begin()->first;
                        auto display_frame = frame_buffer.begin()->second.second;
                        auto timestamp_before_displaying = std::chrono::high_resolution_clock::now();

                        cv::imshow("Receiver", display_frame);
                        cv::waitKey(1);

                        auto timestamp_after_displaying = std::chrono::high_resolution_clock::now();

                        std::chrono::duration<double, std::milli> frame_delay = timestamp_after_displaying - frame_buffer.begin()->second.first;
                        total_delay += frame_delay.count();

                        frames_displayed++;
                        last_displayed_index = display_frame_index;
                        frame_buffer.erase(display_frame_index);
                    }

                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= 15) {
                        double avg_receive_fps = frames_received / 15.0;
                        double avg_display_fps = frames_displayed / 15.0;
                        double avg_delay = total_delay / frames_displayed;

                        std::cout << "Frames Received: " << frames_received << "\n";
                        std::cout << "Frames Displayed: " << frames_displayed << "\n";
                        std::cout << "Average Receive FPS: " << avg_receive_fps << "\n";
                        std::cout << "Average Display FPS: " << avg_display_fps << "\n";
                        std::cout << "Average Delay: " << avg_delay << " ms\n";

                        frames_received = 0;
                        frames_displayed = 0;
                        total_delay = 0.0;
                        start_time = now;
                    }
                }
            }
        }
        catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }

    return 0;
}
