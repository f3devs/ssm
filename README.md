Script kode python untuk menjalankan server:
```
import socket
import struct
import time
import threading
import json
import subprocess
import mss
import time

from io import BytesIO
from PIL import Image, ImageGrab

HOST = '0.0.0.0'
PORT = 5000

TARGET_IMAGE_WIDTH = 1020
JPEG_QUALITY = 50

TARGET_FPS = 20
FRAME_DURATION = 1.0 / TARGET_FPS

def capture_screen(sct, monitor, resized_buf):
    sct_img = sct.grab(monitor)
    img = Image.frombytes('RGB', sct_img.size, sct_img.rgb)
    resized = img.resize((TARGET_IMAGE_WIDTH, resized_buf['height']), Image.BILINEAR)

    jpeg_io = resized_buf['jpeg']
    jpeg_io.seek(0)
    jpeg_io.truncate(0)
    resized.save(jpeg_io, format='JPEG', quality=JPEG_QUALITY)
    jpeg_data = jpeg_io.getvalue()
    return jpeg_data

def handle_input(conn):
    try:
        while True:
            data = conn.recv(1024)
            if not data:
                print("Client Disconnected?")
                break
            
            buffer = data.decode();
            skip = True
            while '\n' in buffer:
                msg, buffer = buffer.split("\n", 1)
                if len(buffer) == 0 or buffer[len(buffer) - 1] != '\n':
                    skip = False
                    break
            
            if skip:
                continue
            
            msg = json.loads(msg)
            x, y = msg.get("x"), msg.get("y")
            # connect pertama kali akan di 0 0
            if x != 0 or y != 0:
                print(f"Click at {x}, {y}")
                subprocess.call(["xdotool", "mousemove", str(x), str(y)])
                subprocess.call(["xdotool", "click", "1"])
                
    except Exception as e:
        print(f"Error: {e}")

def handle_client(conn):
    input_thread = threading.Thread(target=handle_input, args=(conn,), daemon=True)
    input_thread.start()

    with mss.mss() as sct:
        monitor = sct.monitors[1]
        screen_width = monitor['width']
        screen_height = monitor['height']
        aspect_ratio = screen_width / screen_height
        target_height = int(TARGET_IMAGE_WIDTH / aspect_ratio)

        resized_buf = {
            'height': target_height,
            'jpeg': BytesIO()
        }
        
        try:
            while True:
                start = time.time()
                frame = capture_screen(sct, monitor, resized_buf)
                header = struct.pack('!IIIII', TARGET_IMAGE_WIDTH, target_height, screen_width, screen_height, len(frame))
                conn.sendall(header + frame)
                elapsed = time.time() - start
                if elapsed < FRAME_DURATION:
                    time.sleep(FRAME_DURATION - elapsed)
                    
        except Exception as e:
            print(f"Error: {e}")
        finally:
            conn.close()

def start_server():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((HOST, PORT))
        sock.listen(1)
        print(f"Listening on {HOST}:{PORT}")
        global client_connected
        
        while True:
            conn, addr = sock.accept()
            print(f"Connected from {addr}")
            handle_client(conn)

if __name__ == '__main__':
    start_server()
```
