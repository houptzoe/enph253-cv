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

```powershell
python train.py --data dataset/data.yaml --epochs 100
```

Weights are saved to `runs/detect/teletubby/weights/best.pt`.

## 4. Export ONNX for mars-cv

```powershell
python export_onnx.py
```

This copies the model to `../cv-testing/models/teletubby-yolov8n.onnx`.

## 5. Test on PC

From `cv-dev/cv-testing/` after building `mars-cv`:

```powershell
.\build\bin\Release\mars-cv.exe --image test.jpg --model models/teletubby-yolov8n.onnx
.\build\bin\Release\mars-cv.exe --camera --loop --model models/teletubby-yolov8n.onnx
.\build\bin\Release\mars-cv.exe --video clip.mp4 --loop --model models/teletubby-yolov8n.onnx
```

Tune detection with `--confidence 0.5` and `--debounce 3`.

## Retraining

When detection is poor on real toys:

1. Add more labeled photos from your actual setup.
2. Re-run `train.py` and `export_onnx.py`.
3. Redeploy only the updated `.onnx` file — no C++ changes needed.
