from google.oauth2.service_account import Credentials
from googleapiclient.discovery import build
import gspread
import mimetypes
import datetime
from googleapiclient.http import MediaFileUpload
from flask import Flask, request, jsonify, send_from_directory
import os
import re
import cv2
import numpy as np  # noqa: F401 (imported for potential future use)
import pytesseract
import requests


# Flask App
app = Flask(__name__)
UPLOAD_FOLDER = "captured_images"
os.makedirs(UPLOAD_FOLDER, exist_ok=True)


# Google API Setup
scopes = [
    "https://www.googleapis.com/auth/spreadsheets",
    "https://www.googleapis.com/auth/drive",
]
creds = Credentials.from_service_account_file("credentials.json", scopes=scopes)
client = gspread.authorize(creds)
drive_service = build("drive", "v3", credentials=creds)


# Google Sheets & Drive Info
sheet_id = "1KcrG1me5UIWw203CqH1q5YWhv3C3LYUIvHsXx8fhbUQ"
sheet = client.open_by_key(sheet_id)
parent_folder_id = "1HMwzXbjakVOM-oVusTzyr1V8RVNEcD29"


# ESP32 & Blynk Config
ESP32_IP = "http://192.168.254.111"
BLYNK_AUTH = "b189l6OU64UNP8s1R9JgfIJLwqZocuMr"


# Tesseract OCR Config
pytesseract.pytesseract.tesseract_cmd = r"C:\\Program Files\\Tesseract-OCR\\tesseract.exe"


# Public URL
PUBLIC_URL = "https://205b-216-247-24-239.ngrok-free.app"
ANNOTATED_IMAGE_FILENAME = "latest_vehicle_annotated.jpg"


def upload_image(file_path: str, folder_id: str) -> str:
    """Uploads an image to Google Drive and returns a public direct link."""
    file_name = os.path.basename(file_path)
    mime_type = mimetypes.guess_type(file_path)[0] or "application/octet-stream"

    file_metadata = {
        "name": file_name,
        "parents": [folder_id],
    }
    media = MediaFileUpload(file_path, mimetype=mime_type)
    uploaded_file = (
        drive_service.files()
        .create(body=file_metadata, media_body=media, fields="id")
        .execute()
    )

    file_id = uploaded_file.get("id")
    drive_service.permissions().create(
        fileId=file_id,
        body={"role": "reader", "type": "anyone"},
    ).execute()

    return f"https://drive.google.com/uc?id={file_id}"


def preprocess_image(image_path: str):
    """Preprocesses the image for better OCR results."""
    image = cv2.imread(image_path)
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    blurred = cv2.GaussianBlur(gray, (5, 5), 0)
    thresh = cv2.adaptiveThreshold(
        blurred,
        255,
        cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv2.THRESH_BINARY,
        11,
        2,
    )

    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if contours:
        max_contour = max(contours, key=cv2.contourArea)
        x, y, w, h = cv2.boundingRect(max_contour)
        cropped = gray[y : y + h, x : x + w]
    else:
        cropped = gray

    return cv2.threshold(cropped, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)[1]


def filter_extracted_text(text: str) -> str:
    """Filters extracted text to remove unwanted characters."""
    words = re.findall(r"\b[a-zA-Z0-9]{3,}\s+[a-zA-Z0-9]{3,}\b", text)
    return " ".join(words)


def extract_text(image_path: str) -> str:
    """Extracts text from the image using OCR."""
    processed_image = preprocess_image(image_path)
    raw_text = pytesseract.image_to_string(
        processed_image, lang="eng", config="--psm 6"
    ).strip()
    return filter_extracted_text(raw_text)


def overlay_text(image_path: str, text: str) -> str | None:
    """Overlays extracted text on the image and saves annotated copy."""
    img = cv2.imread(image_path)
    if img is None:
        return None

    font = cv2.FONT_HERSHEY_DUPLEX
    font_scale = 4
    font_thickness = 2
    text_color = (0, 255, 0)
    bg_color = (0, 0, 0)

    (text_width, text_height), baseline = cv2.getTextSize(
        text, font, font_scale, font_thickness
    )
    x = img.shape[1] - text_width - 20
    y = img.shape[0] - 20

    cv2.rectangle(
        img,
        (x - 5, y - text_height - 5),
        (x + text_width + 5, y + baseline + 5),
        bg_color,
        -1,
    )
    cv2.putText(img, text, (x, y), font, font_scale, text_color, font_thickness)

    annotated_image_path = os.path.join(UPLOAD_FOLDER, ANNOTATED_IMAGE_FILENAME)
    cv2.imwrite(annotated_image_path, img)
    return annotated_image_path


@app.route("/capture", methods=["POST"])
def capture():
    """Handles image capture, OCR, and data logging from ESP32-CAM."""
    try:
        image_data = request.data
        if not image_data:
            return jsonify({"status": "error", "message": "No image received"}), 400

        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        image_filename = f"vehicle_{timestamp.replace(':', '-')}.jpg"
        image_path = os.path.join(UPLOAD_FOLDER, image_filename)

        with open(image_path, "wb") as image_file:
            image_file.write(image_data)

        detected_text = extract_text(image_path)
        print(f"Extracted Text: {detected_text}")

        # Send extracted text to ESP32 (which listens on /receive_text)
        try:
            response = requests.get(
                f"{ESP32_IP}/receive_text", params={"text": detected_text}, timeout=3
            )
            print(f"ESP32 Response: {response.text}")
        except Exception as esp_err:
            print(f"ESP32 notify failed: {esp_err}")

        annotated_image_path = overlay_text(image_path, detected_text)
        if not annotated_image_path:
            return jsonify({"status": "error", "message": "Failed to process image"}), 500

        image_drive_link = upload_image(annotated_image_path, parent_folder_id)
        annotated_image_url = f"{PUBLIC_URL}/uploads/{ANNOTATED_IMAGE_FILENAME}"

        # Save Data to Google Sheets
        worksheet = sheet.worksheet("Sheet1")
        worksheet.append_rows([[timestamp, detected_text, image_drive_link]])

        return (
            jsonify(
                {
                    "status": "success",
                    "timestamp": timestamp,
                    "extracted_text": detected_text,
                    "image_drive_link": image_drive_link,
                    "annotated_image_url": annotated_image_url,
                }
            ),
            200,
        )
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


@app.route("/uploads/latest_vehicle_annotated.jpg")
def get_annotated_image():
    """Serves the latest annotated image."""
    return send_from_directory(UPLOAD_FOLDER, ANNOTATED_IMAGE_FILENAME)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)


