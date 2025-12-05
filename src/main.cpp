#include <jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window_jni.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <csetjmp>
#include <jpeglib.h>
#include <turbojpeg.h>

#include <android_native_app_glue.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "ScreenClient", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ScreenClient", __VA_ARGS__)

ANativeWindow* window = nullptr;
int sockfd = -1;
int width = 800, height = 480;

const char* server_ip = "192.168.100.1";
const unsigned int port = 5000;

int serverWidth = 0;
int serverHeight = 0;
int drawW = 0;
int drawH = 0;
int offsetX = 0;
int offsetY = 0;
int imageWidth = 0;
int imageHeight = 0;

bool connectToServer() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return false;

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);

    return connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) == 0;
}

// Fungsi untuk menerima `n` byte dari soket
bool recv_all(void* buffer, size_t size) {
    size_t total = 0;
    char* ptr = (char*)buffer;
    while (total < size) {
        ssize_t received = recv(sockfd, ptr + total, size - total, 0);
        if (received <= 0) return false;
        total += received;
    }
    return true;
}

void receiveLoop() {
    tjhandle tjInstance = tjInitDecompress();
    // Buffer untuk gambar RGB
    std::vector<unsigned char> rgbBuffer;

    while (true) {
        uint32_t t1, t2;
        if (!recv_all(&t1, 4)) break;
        if (!recv_all(&t2, 4)) break;
        imageWidth = ntohl(t1);
        imageHeight = ntohl(t2);

        if (!recv_all(&t1, 4)) break;
        if (!recv_all(&t2, 4)) break;
        serverWidth = ntohl(t1);
        serverHeight = ntohl(t2);

        // Terima ukuran JPEG
        uint32_t jpegLenNet;
        if (!recv_all(&jpegLenNet, 4)) break;
        uint32_t jpegLen = ntohl(jpegLenNet);

        std::vector<unsigned char> jpegBuf(jpegLen);
        if (!recv_all(jpegBuf.data(), jpegLen)) break;

        // Decode JPEG (turbojpeg)
        int jpegSubsamp, jpegColorspace;
        int width, height;
        tjDecompressHeader3(tjInstance, jpegBuf.data(), jpegLen, &width, &height, &jpegSubsamp, &jpegColorspace);

        rgbBuffer.resize(width * height * 3);
        tjDecompress2(tjInstance, jpegBuf.data(), jpegLen, rgbBuffer.data(), width, 0 /* pitch */, height, TJPF_RGB, TJFLAG_FASTDCT);

        // Render ke layar
        if (window) {
            ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGB_565);
            ANativeWindow_Buffer buffer;
            if (ANativeWindow_lock(window, &buffer, nullptr) == 0) {
                uint16_t* dst = (uint16_t*)buffer.bits;
                int screenW = buffer.width;
                int screenH = buffer.height;

                float screenRatio = (float)screenW / screenH;
                float imageRatio  = (float)imageWidth / imageHeight;

                if (imageRatio > screenRatio) {
                    drawW = screenW;
                    drawH = (int)(screenW / imageRatio);
                    offsetX = 0;
                    offsetY = (screenH - drawH) / 2;
                } else {
                    drawH = screenH;
                    drawW = (int)(screenH * imageRatio);
                    offsetY = 0;
                    offsetX = (screenW - drawW) / 2;
                }

                // Clear: isi buffer dengan warna hitam
                memset(dst, 0, buffer.stride * buffer.height * sizeof(uint16_t));

                // Salin gambar (dengan offset + scaling)
                for (int y = 0; y < drawH; y++) {
                    for (int x = 0; x < drawW; x++) {
                        int srcX = x * width / drawW;
                        int srcY = y * height / drawH;
                        int srcIndex = (srcY * width + srcX) * 3;

                        uint8_t r = rgbBuffer[srcIndex + 0];
                        uint8_t g = rgbBuffer[srcIndex + 1];
                        uint8_t b = rgbBuffer[srcIndex + 2];

                        uint16_t pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                        int dstIndex = (y + offsetY) * buffer.stride + (x + offsetX);
                        dst[dstIndex] = pixel;
                    }
                }
                ANativeWindow_unlockAndPost(window);
                /// Kirim respon ke server. misalnya frame/image sudah selesai di proses
				char msg[128] = "{}";
				send(sockfd, msg, strlen(msg), 0);
            }
        }
    }
	char msg[128] = "{\"disconnect\":true}";
	send(sockfd, msg, strlen(msg), 0);
    close(sockfd);
}

// ======================= Touch Handler ========================

void sendTouch(float x, float y) {
    char msg[128];
    snprintf(msg, 128, "{\"disconnect\":false,\"x\":%d,\"y\":%d}", (int)x, (int)y);
    send(sockfd, msg, strlen(msg), 0);
}

// ======================= Entry Point ========================

void android_main(struct android_app* app) {
    app->onAppCmd = [](android_app* app, int32_t cmd) {
        if (cmd == APP_CMD_INIT_WINDOW) {
        	LOG("APP_CMD_INIT_WINDOW");
            window = app->window;
            std::thread([] {
                if (connectToServer()) {
                    receiveLoop();
                }
            }).detach();
        } else if (cmd == APP_CMD_CONFIG_CHANGED) {
        	LOG("APP_CMD_CONFIG_CHANGED");
        } else if (cmd == APP_CMD_WINDOW_RESIZED) {
        	LOG("APP_CMD_WINDOW_RESIZED");
        } // ...
    };

    app->onInputEvent = [](android_app* app, AInputEvent* event) {
    	if (sockfd > 0) {
			/// handle touch screen
		    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION &&
		        AMotionEvent_getAction(event) == AMOTION_EVENT_ACTION_DOWN) {
		        float x = AMotionEvent_getX(event, 0);
		        float y = AMotionEvent_getY(event, 0);
		        if (x < offsetX || x > offsetX + drawW ||
		            y < offsetY || y > offsetY + drawH) {
		            // Di luar gambar, abaikan
		        } else {
		            int desktopX = (int) ((x - offsetX) / (ANativeWindow_getWidth(window) - (offsetX * 2)) * serverWidth);
		            int desktopY = (int) (y / ANativeWindow_getHeight(window) * serverHeight);
		            // Kirim desktopX, desktopY ke server
		            sendTouch(desktopX, desktopY);
		        }

		        return 1;
		    }
        }
        return 0;
    };

    while (true) {
        int events;
        android_poll_source* source;
        while (ALooper_pollAll(-1, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                LOG("App destroy requested");
                if (sockfd > 0) {
					char msg[128] = "{\"disconnect\":true}";
					send(sockfd, msg, strlen(msg), 0);
					close(sockfd);
				}
                return;
            }
        }
    }
}
