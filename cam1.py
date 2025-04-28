from picamera2 import Picamera2, Preview
import time

picam2 = Picamera2()

# Use the QT-based preview if DRM isn't working
picam2.start_preview(Preview.QT)

picam2.configure(picam2.create_preview_configuration())
picam2.start()

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("Stopping...")
    picam2.stop()
