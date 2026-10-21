import asyncio
import cv2
import time
import websockets
from pathlib import Path
from ultralytics import YOLO

ESP32_IP = "10.201.216.205"
ESP32_PORT = 81
SCREEN_WIDTH = 320
SCREEN_HEIGHT = 240
JPEG_QUALITY = 40 
TARGET_FPS = 16
MODEL_PATH = Path(__file__).with_name("best.pt")
CONFIDENCE_THRESHOLD = 0.1
CAMERA_INDEX = 0
WINDOW_NAME = "Person Detection"

async def send_camera():
    uri = f"ws://{ESP32_IP}:{ESP32_PORT}"
    model = YOLO(str(MODEL_PATH))
    camera = cv2.VideoCapture(CAMERA_INDEX)
    if not camera.isOpened():
        camera.release()
        raise RuntimeError(f"Could not open camera {CAMERA_INDEX}")

    websocket = None
    connection_task = None
    next_connection_attempt = 0.0
    previous_detection = None

    try:
        while True:
            frame_start = time.monotonic()
            if websocket is None:
                if connection_task is None and time.monotonic() >= next_connection_attempt:
                    print(f"Connecting to ESP32: {uri}")
                    connection_task = asyncio.ensure_future(
                        websockets.connect(uri, ping_interval=None, open_timeout=3)
                    )
                elif connection_task is not None and connection_task.done():
                    try:
                        websocket = connection_task.result()
                        previous_detection = None
                        print("Connected to ESP32.")
                    except (OSError, websockets.exceptions.WebSocketException) as error:
                        print(f"Could not connect to ESP32: {error}. Trying again...")
                        next_connection_attempt = time.monotonic() + 2
                    finally:
                        connection_task = None

            success, frame = camera.read()
            if not success:
                raise RuntimeError("Could not read a frame from the camera")

            result = model.predict(
                source=frame,
                conf=CONFIDENCE_THRESHOLD,
                imgsz=320,
                verbose=False,
            )[0]
            classes = result.boxes.cls.tolist() if result.boxes is not None else []
            person_detected = any(
                str(model.names[int(class_id)]).strip().lower() == "person"
                for class_id in classes
            )

            display_frame = result.plot()
            status_label = "PERSON DETECTED" if person_detected else "PERSON NOT DETECTED"
            status_color = (0, 200, 0) if person_detected else (0, 0, 255)
            cv2.putText(
                display_frame,
                status_label,
                (10, 35),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                status_color,
                2,
            )
            cv2.imshow(WINDOW_NAME, display_frame)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break

            if websocket is not None:
                try:
                    if person_detected != previous_detection:
                        status = "person detected" if person_detected else "person not detected"
                        await websocket.send(status)
                        previous_detection = person_detected

                    resized_frame = cv2.resize(frame, (SCREEN_WIDTH, SCREEN_HEIGHT))
                    encoded, jpeg_data = cv2.imencode(
                        ".jpg",
                        resized_frame,
                        [int(cv2.IMWRITE_JPEG_QUALITY), JPEG_QUALITY],
                    )
                    if not encoded:
                        raise RuntimeError("Could not encode the camera frame as JPEG")

                    jpeg_bytes = jpeg_data.tobytes()
                    await websocket.send(f"JPEG_FRAME_SIZE:{len(jpeg_bytes)}")
                    await websocket.send(jpeg_bytes)
                except (OSError, websockets.exceptions.WebSocketException) as error:
                    print(f"ESP32 connection lost: {error}. Reconnecting...")
                    await websocket.close()
                    websocket = None
                    previous_detection = None
                    next_connection_attempt = time.monotonic() + 2

            frame_time = time.monotonic() - frame_start
            await asyncio.sleep(max(0, (1 / TARGET_FPS) - frame_time))
    finally:
        if connection_task is not None:
            if not connection_task.done():
                connection_task.cancel()
                try:
                    await connection_task
                except asyncio.CancelledError:
                    pass
            elif not connection_task.cancelled():
                connection_error = connection_task.exception()
                if connection_error is not None:
                    print(f"ESP32 connection attempt ended: {connection_error}")
                else:
                    await connection_task.result().close()
        if websocket is not None:
            await websocket.close()
        camera.release()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    asyncio.run(send_camera())