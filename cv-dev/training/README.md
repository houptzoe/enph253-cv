# Teletubby model training (PC only)

Python scripts for labeling, training, and exporting the ONNX model used by `mars-cv`.
The Raspberry Pi runs C++ inference only — it does not need Python or PyTorch.

## Setup

```powershell
cd cv-dev/training
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

## 1. Collect training images

Target **200–400 photos** of your physical teletubby figurines before competition day.

### What to capture

- All teletubby colors you expect on the course
- Distances: ~0.3 m, 1 m, and 2 m
- Angles: front, side, partial occlusion
- Lighting: bright, dim, and shadowed scenes
- **Negative images (~30%)**: course background with no teletubby visible

### Efficient capture

1. Record 2–3 minute videos while moving the toy through the frame.
2. Extract frames:

   ```powershell
   python extract_frames.py path\to\video.mp4 --output dataset/raw --every 10
   ```

3. Copy any standalone photos into `dataset/raw/` as well.

### Bootstrap (pipeline test only)

Download ~50 web images of teletubby toys to verify train → export → C++ works.
**Retrain on real figurine photos** before trusting detection on the course.

## 2. Label in Roboflow

1. Create a free project at [roboflow.com](https://roboflow.com).
2. Upload images from `dataset/raw/`.
3. Draw bounding boxes with class name **`teletubby`** (single class).
4. Export dataset in **YOLOv8** format.
5. Unzip into `dataset/` so you have:
   - `dataset/data.yaml`
   - `dataset/images/train`, `dataset/images/val`
   - `dataset/labels/train`, `dataset/labels/val`

See `dataset/data.yaml.example` for the expected layout.

## 3. Train

Default dataset folder `dataset/`:

```powershell
python train.py --data dataset/data.yaml --epochs 100
```

Or point at a named dataset folder (e.g. `320-dataset/`):

```powershell
python train.py --dataset 320-dataset --epochs 100
```

That sets:
- `--data 320-dataset/data.yaml`
- run name `teletubby-320` → `runs/detect/teletubby-320/weights/best.pt`
- `--imgsz 320` automatically when the folder/name contains `320`

## 4. Export ONNX for mars-cv

Default (640) model:

```powershell
python export_onnx.py
```

→ `../cv-testing/models/teletubby-yolov8n.onnx`

320-tagged model:

```powershell
python export_onnx.py --tag 320
```

→ `../cv-testing/models/teletubby-yolov8n-320.onnx` (imgsz 320)

## 5. Test on PC

From `cv-dev/cv-testing/` after building `mars-cv`:

```powershell
.\build\bin\Release\mars-cv.exe --image test.jpg --model models/teletubby-yolov8n.onnx
.\build\bin\Release\mars-cv.exe --camera --loop --model models/teletubby-yolov8n.onnx
.\build\bin\Release\mars-cv.exe --video clip.mp4 --loop --model models/teletubby-yolov8n.onnx
```

For the 320 model, use `models/teletubby-yolov8n-320.onnx` and match YOLO input size in `mars-cv` (letterbox 320).

Tune detection with `--confidence 0.85`, `--window 8`, `--hit-rate 0.7`, and `--warmup 20`.

## Retraining

When detection is poor on real toys:

1. Add more labeled photos from your actual setup.
2. Re-run `train.py` (with `--dataset ...` if needed) and `export_onnx.py --tag ...`.
3. Redeploy only the updated `.onnx` file — no C++ changes needed unless input size changed.
