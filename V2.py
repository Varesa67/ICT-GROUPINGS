import asyncio
import cv2
import os
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
AUDIO_PATH = Path(__file__).with_name("recording.m4a")
CONFIDENCE_THRESHOLD = 0.1
CAMERA_INDEX = 0
WINDOW_NAME = "Person, Intruder, and Fire Detection"

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
    previous_detections = {
        "person a": None,
        "person b": None,
        "intruder": None,
        "fire": None,
    }
    previous_person_b_detected = False

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
                        previous_detections = {
                            "person a": None,
                            "person b": None,
                            "intruder": None,
                            "fire": None,
                        }
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
            detected_labels = {
                " ".join(
                    str(model.names[int(class_id)])
                    .strip()
                    .lower()
                    .replace("_", " ")
                    .replace("-", " ")
                    .split()
                )
                for class_id in classes
            }
            current_detections = {
                "person a": "person a" in detected_labels,
                "person b": "person b" in detected_labels,
                "intruder": bool(
                    detected_labels.intersection({"intruder", "intruders"})
                ),
                "fire": "fire" in detected_labels,
            }

            if current_detections["person b"] and not previous_person_b_detected:
                if not AUDIO_PATH.is_file():
                    print(f"Person B detected, but audio file was not found: {AUDIO_PATH}")
                else:
                    try:
                        os.startfile(str(AUDIO_PATH))
                    except OSError as error:
                        print(f"Could not play Person B audio: {error}")
            previous_person_b_detected = current_detections["person b"]

            display_frame = result.plot()
            cv2.putText(
                display_frame,
                "PERSON A: DETECTED" if current_detections["person a"] else "PERSON A: NOT DETECTED",
                (10, 35),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                (0, 200, 0) if current_detections["person a"] else (0, 0, 255),
                2,
            )
            cv2.putText(
                display_frame,
                "PERSON B: DETECTED" if current_detections["person b"] else "PERSON B: NOT DETECTED",
                (10, 60),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                (0, 200, 0) if current_detections["person b"] else (0, 0, 255),
                2,
            )
            cv2.putText(
                display_frame,
                "INTRUDER DETECTED" if current_detections["intruder"] else "NO INTRUDER",
                (10, 85),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                (0, 0, 255) if current_detections["intruder"] else (0, 200, 0),
                2,
            )
            cv2.putText(
                display_frame,
                "FIRE DETECTED" if current_detections["fire"] else "NO FIRE",
                (10, 110),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                (0, 0, 255) if current_detections["fire"] else (0, 200, 0),
                2,
            )
            cv2.imshow(WINDOW_NAME, display_frame)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break

            if websocket is not None:
                try:
                    for label, detected in current_detections.items():
                        if detected != previous_detections[label]:
                            status = f"{label} {'detected' if detected else 'not detected'}"
                            await websocket.send(status)
                            previous_detections[label] = detected

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
                    previous_detections = {
                        "person a": None,
                        "person b": None,
                        "intruder": None,
                        "fire": None,
                    }
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