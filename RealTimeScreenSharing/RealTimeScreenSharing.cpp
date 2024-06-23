/*
// Sender

#include <iostream>
#include <opencv2/opencv.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <thread>

using boost::asio::ip::tcp;

cv::Mat captureScreen() {
    HWND hwndDesktop = GetDesktopWindow();
    HDC hdcDesktop = GetDC(hwndDesktop);

    HDC hdcMemory = CreateCompatibleDC(hdcDesktop);
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcDesktop, screenWidth, screenHeight);

    SelectObject(hdcMemory, hBitmap);
    BitBlt(hdcMemory, 0, 0, screenWidth, screenHeight, hdcDesktop, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = screenWidth;
    bi.biHeight = -screenHeight;  // negative to ensure top-down drawing
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

int main(int argc, char* argv[]) {
	const char* host = "127.0.0.1";
	int port = std::stoi("8008");
	int fps = std::stoi("165");
	int delay = 1000 / fps;  // Delay in milliseconds

    try {
        boost::asio::io_service io_service;
        tcp::socket socket(io_service);
        tcp::resolver resolver(io_service);
        boost::asio::connect(socket, resolver.resolve({host, std::to_string(port)}));

        cv::Mat frame;
        std::vector<uchar> buffer;
        uint64_t transmitted_bits = 0;
        uint32_t frame_index = 0;
        uint32_t frames_sent = 0;
        auto start_time = std::chrono::steady_clock::now();

        while (true) {
            auto timestamp_before_capture = std::chrono::high_resolution_clock::now(); // Before capturing image
            int64_t timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp_before_capture.time_since_epoch()).count();

            frame = captureScreen();

            auto timestamp_after_capture = std::chrono::high_resolution_clock::now(); // After capturing and before encoding
            if (frame.empty()) {
                std::cerr << "Error: Captured frame is empty.\n";
                continue;
            }

            cv::imencode(".jpg", frame, buffer);

            auto timestamp_before_sending = std::chrono::high_resolution_clock::now(); // After encoding and before sending
            uint32_t size = buffer.size();

            // Send the timestamp, frame index, and size first
            boost::asio::write(socket, boost::asio::buffer(&timestamp_ms, sizeof(timestamp_ms)));
            boost::asio::write(socket, boost::asio::buffer(&frame_index, sizeof(frame_index)));
            boost::asio::write(socket, boost::asio::buffer(&size, sizeof(size)));

            // Send the actual image data
            boost::asio::write(socket, boost::asio::buffer(buffer.data(), size));

            transmitted_bits += size * 8;
            frame_index++;
            frames_sent++;

            auto timestamp_after_sending = std::chrono::high_resolution_clock::now(); // After sending

            std::chrono::duration<double, std::milli> capture_duration = timestamp_after_capture - timestamp_before_capture;
            std::chrono::duration<double, std::milli> encode_duration = timestamp_before_sending - timestamp_after_capture;
            std::chrono::duration<double, std::milli> send_duration = timestamp_after_sending - timestamp_before_sending;

            std::this_thread::sleep_for(std::chrono::milliseconds(delay) - (timestamp_after_sending - timestamp_before_capture));

            // Calculate and print statistics every minute
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::minutes>(now - start_time).count() >= 1) {
                double avg_fps = frames_sent / 60.0; // since it's 1 minute
                uint64_t transmitted_megas = transmitted_bits / 8000000;
                std::cout << "Frames Sent: " << frames_sent << "\n";
                std::cout << "Average FPS: " << avg_fps << "\n";
                std::cout << "Total Transmitted Megabytes: " << transmitted_megas << " Megabytes\n";
                frames_sent = 0;
                start_time = now;
            }
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
*/
///*
// Receiver:
#include <iostream>
#include <opencv2/opencv.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <unordered_map>

using boost::asio::ip::tcp;
using FrameData = std::pair<std::chrono::high_resolution_clock::time_point, cv::Mat>;

std::unordered_map<uint32_t, FrameData> frame_buffer;
uint32_t last_displayed_index = 0;

void logTime(const std::string& label, std::chrono::high_resolution_clock::time_point start, std::chrono::high_resolution_clock::time_point end) {
    std::chrono::duration<double, std::milli> duration = end - start;
    std::cout << label << " time: " << duration.count() << " ms\n";
}

int main(int argc, char* argv[]) {
	int port = std::stoi("8008");

    try {
        boost::asio::io_service io_service;
        tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), port));
        tcp::socket socket(io_service);

        acceptor.accept(socket);

        uint32_t size;
        std::vector<uchar> buffer;
        cv::Mat frame;
        uint32_t frames_received = 0;
        uint32_t frames_displayed = 0;
        auto start_time = std::chrono::steady_clock::now();
        double total_delay = 0.0;

        while (true) {
            int64_t timestamp_ms;
            uint32_t frame_index;

            auto timestamp_before_receiving = std::chrono::high_resolution_clock::now(); // Before receiving

            boost::asio::read(socket, boost::asio::buffer(&timestamp_ms, sizeof(timestamp_ms)));
            boost::asio::read(socket, boost::asio::buffer(&frame_index, sizeof(frame_index)));
            boost::asio::read(socket, boost::asio::buffer(&size, sizeof(size)));
            buffer.resize(size);
            boost::asio::read(socket, boost::asio::buffer(buffer.data(), size));

            auto timestamp_after_receiving = std::chrono::high_resolution_clock::now(); // After receiving and before decoding

            frame = cv::imdecode(buffer, cv::IMREAD_COLOR);

            if (frame.empty()) {
                std::cerr << "Error: Decoded frame is empty.\n";
                continue;
            }

            auto timestamp_after_decoding = std::chrono::high_resolution_clock::now(); // After decoding
            frame_buffer[frame_index] = std::make_pair(std::chrono::high_resolution_clock::time_point(std::chrono::milliseconds(timestamp_ms)), frame);

            frames_received++;

            // Check for the latest frame to display
            while (!frame_buffer.empty() && frame_buffer.begin()->first <= last_displayed_index) {
                frame_buffer.erase(frame_buffer.begin());
            }

            if (!frame_buffer.empty()) {
                auto display_frame_index = frame_buffer.begin()->first;
                auto display_frame = frame_buffer.begin()->second.second;
                auto timestamp_before_displaying = std::chrono::high_resolution_clock::now();

                cv::imshow("Receiver", display_frame);
                cv::waitKey(1);  // 1 ms delay to process events

                auto timestamp_after_displaying = std::chrono::high_resolution_clock::now(); // After displaying

                std::chrono::duration<double, std::milli> frame_delay = timestamp_after_displaying - frame_buffer.begin()->second.first;
                total_delay += frame_delay.count();

                frames_displayed++;
                last_displayed_index = display_frame_index;
                frame_buffer.erase(display_frame_index);
            }

            // Calculate and print statistics every minute
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::minutes>(now - start_time).count() >= 1) {
                double avg_receive_fps = frames_received / 60.0; // since it's 1 minute
                double avg_display_fps = frames_displayed / 60.0; // since it's 1 minute
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
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
//*/