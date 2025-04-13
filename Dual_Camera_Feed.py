import cv2
from picamera2 import Picamera2

#initialise cameras
picam0 = Picamera2(0)
picam1 = Picamera2(1)

#change or set resolution
#picam2.preview_configuration.main.size = (640, 480)
picam0.preview_configuration.main.format = "RGB888"
#can change from preview to still/video/low res
picam0.configure("preview")
picam0.start()

picam1.preview_configuration.main.format = "RGB888"
picam1.configure("preview")
picam1.start()

while True:
    frame0 = picam0.capture_array()
    frame1 = picam1.capture_array()
    cv2.imshow("Wide_Camera", frame0)
    cv2.imshow("Standard_Camera", frame1)

    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cv2.destroyAllWindows()
picam0.close()
picam1.close()
