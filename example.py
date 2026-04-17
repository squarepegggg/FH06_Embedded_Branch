import tensorflow as tf
import numpy as np
import pandas as pd
import glob
import json
import os
from pathlib import Path
from collections import Counter

print("TensorFlow:", tf.__version__)

# ---- Config ----
INCLUDE_LABELS = {"67", "disco", "roll", "griddy", "idle"}  # 5-class setup
IDLE_LABEL = "idle"

WINDOW_SIZE = 50              # 2.5s at ~20 Hz
TRAIN_STRIDE = 12
NORMALIZATION_SCALE = 2048.0  # raw accel range ~[-2048, 2048]; MCU must divide by the same constant
IDLE_STD_THRESHOLD = 80.0     # lowered from 120 so smaller-amplitude motions survive segmentation
TEST_PERSON = "Matthew"       # held out; has all selected gestures + idle
EPOCHS = 25
BATCH_SIZE = 32
AUG_COPIES = 2                # number of augmented copies per training window (0 disables aug)


def parse_filename(stem):
    """Handle both "Ronak Dab" (space) and "Ronak_idle" (underscore) naming."""
    for sep in (" ", "_"):
        if sep in stem:
            person, label = stem.split(sep, 1)
            return person.strip(), label.strip().lower()
    return "unknown", stem.lower()


def load_csv_data(csv_dir="ML"):
    """Load all CSV files, filtered to INCLUDE_LABELS."""
    csv_files = glob.glob(os.path.join(csv_dir, "*.csv"))
    records = []
    skipped = Counter()
    for csv_file in csv_files:
        person, label = parse_filename(Path(csv_file).stem)
        if label not in INCLUDE_LABELS:
            skipped[label] += 1
            continue
        df = pd.read_csv(csv_file)
        data = df[["X", "Y", "Z"]].values.astype(np.float32)
        records.append((data, label, person))
    if skipped:
        print(f"Skipped files for excluded labels: {dict(skipped)}")
    return records


def window_is_active(window):
    """Orientation-independent motion check: std of |a| across the window.
    Computed on RAW values (before normalization)."""
    mag = np.sqrt(np.sum(window * window, axis=1))
    return float(mag.std()) >= IDLE_STD_THRESHOLD


def create_windows(records, window_size, stride):
    """Slide over each recording. Windows are stored as RAW accel values — the model's
    first layer handles normalization, so firmware can pass raw data straight through."""
    X, y = [], []
    for data, label, _person in records:
        if len(data) < window_size:
            continue
        for i in range(0, len(data) - window_size + 1, stride):
            window = data[i:i + window_size]  # (W, 3)
            if label == IDLE_LABEL:
                effective_label = IDLE_LABEL  # trust dedicated idle recordings
            else:
                effective_label = label if window_is_active(window) else IDLE_LABEL
            w = window.T.reshape(3, window_size, 1)  # raw, no normalization
            X.append(w)
            y.append(effective_label)
    return np.array(X, dtype=np.float32), np.array(y)


def augment_window(window, rng):
    """window shape (3, W, 1) with RAW accelerometer values."""
    w = window.copy()
    # Time shift: small circular roll along the time axis
    shift = int(rng.integers(-3, 4))
    if shift != 0:
        w = np.roll(w, shift, axis=1)
    # Magnitude scaling: ±10%, one scalar across the whole window
    w = w * float(rng.uniform(0.9, 1.1))
    # Gaussian noise in raw count space (~1% of full scale)
    w = w + rng.normal(0.0, 20.0, size=w.shape).astype(np.float32)
    return w.astype(np.float32)


def augment_dataset(X, y_strings, n_copies, seed=123):
    """Produce n_copies augmented replicas of each training window. Original set is kept."""
    if n_copies <= 0:
        return X, y_strings
    rng = np.random.default_rng(seed)
    X_all = [X]
    y_all = [y_strings]
    for _ in range(n_copies):
        X_new = np.stack([augment_window(x, rng) for x in X])
        X_all.append(X_new)
        y_all.append(y_strings)
    return np.concatenate(X_all, axis=0), np.concatenate(y_all, axis=0)


def balance_class(X, y_strings, cap_label, target_count):
    """Cap one class at target_count (used for idle, which can blow up)."""
    cap_idx = np.where(y_strings == cap_label)[0]
    other_idx = np.where(y_strings != cap_label)[0]
    if len(cap_idx) > target_count:
        rng = np.random.default_rng(42)
        cap_idx = rng.choice(cap_idx, size=target_count, replace=False)
    keep = np.concatenate([other_idx, cap_idx])
    keep.sort()
    return X[keep], y_strings[keep]


# ---- Load and split by person ----
print("Loading CSV files...")
records = load_csv_data()
train_records = [r for r in records if r[2] != TEST_PERSON]
test_records = [r for r in records if r[2] == TEST_PERSON]
print(f"Train files: {len(train_records)} | Test files (person={TEST_PERSON}): {len(test_records)}")
print("Train files by person:", Counter(r[2] for r in train_records))
print("Train files by label:", Counter(r[1] for r in train_records))

# ---- Window ----
print("Creating train windows...")
X_train, y_train_s = create_windows(train_records, WINDOW_SIZE, TRAIN_STRIDE)
print("Creating test windows...")
X_test, y_test_s = create_windows(test_records, WINDOW_SIZE, TRAIN_STRIDE)

# Cap idle to the average gesture-class size so it doesn't drown gestures
train_counts = Counter(y_train_s)
gesture_counts = [c for lab, c in train_counts.items() if lab != IDLE_LABEL]
avg_gesture = int(np.mean(gesture_counts)) if gesture_counts else 0
X_train, y_train_s = balance_class(X_train, y_train_s, IDLE_LABEL, target_count=avg_gesture)
X_test, y_test_s = balance_class(X_test, y_test_s, IDLE_LABEL, target_count=max(1, avg_gesture // 3))

# Augment only the training set — never the test set
n_before = len(X_train)
X_train, y_train_s = augment_dataset(X_train, y_train_s, n_copies=AUG_COPIES)
print(f"Augmentation: {n_before} -> {len(X_train)} train windows ({AUG_COPIES} extra copies per original)")

# Label encoding — union so train/test share the same index space
unique_labels = sorted(set(y_train_s.tolist()) | set(y_test_s.tolist()))
label_to_int = {lab: i for i, lab in enumerate(unique_labels)}
y_train = np.array([label_to_int[l] for l in y_train_s], dtype=np.int32)
y_test = np.array([label_to_int[l] for l in y_test_s], dtype=np.int32)

NUM_CLASSES = len(unique_labels)
print(f"Classes ({NUM_CLASSES}): {unique_labels}")
print(f"Train windows: {len(X_train)}  Test windows: {len(X_test)}")
print("Train class distribution:", dict(Counter(y_train_s)))
print("Test class distribution:", dict(Counter(y_test_s)))


class WindowNormalizeLayer(tf.keras.layers.Layer):
    """Per-window, per-axis mean subtraction + fixed scale. Input (B, 3, W, 1) → same shape.
    Baked into the TFLite graph so the firmware can feed raw accelerometer values directly."""

    def __init__(self, scale, **kwargs):
        super().__init__(**kwargs)
        self.scale = float(scale)

    def call(self, inputs):
        mean = tf.reduce_mean(inputs, axis=2, keepdims=True)  # (B, 3, 1, 1)
        return (inputs - mean) * (1.0 / self.scale)

    def get_config(self):
        return {**super().get_config(), "scale": self.scale}


def build_1dcnn_classifier(num_classes: int, window_size: int):
    inputs = tf.keras.Input(shape=(3, window_size, 1), name=f"input_raw_3x{window_size}x1")
    x = WindowNormalizeLayer(NORMALIZATION_SCALE, name="normalize")(inputs)
    x = tf.keras.layers.Reshape((3 * window_size, 1), name="reshape_to_seq")(x)

    x = tf.keras.layers.Conv1D(32, 5, padding="same", activation="relu")(x)
    x = tf.keras.layers.MaxPool1D(2)(x)

    x = tf.keras.layers.Conv1D(8, 3, padding="same", activation="relu")(x)
    x = tf.keras.layers.MaxPool1D(2)(x)

    x = tf.keras.layers.Conv1D(8, 3, padding="same", activation="relu")(x)
    x = tf.keras.layers.MaxPool1D(2)(x)

    x = tf.keras.layers.Conv1D(8, 3, padding="same", activation="relu")(x)
    x = tf.keras.layers.GlobalAveragePooling1D()(x)

    x = tf.keras.layers.Dense(64, activation="relu")(x)
    x = tf.keras.layers.Dropout(0.2)(x)

    outputs = tf.keras.layers.Dense(num_classes, activation="softmax", name="probs")(x)
    return tf.keras.Model(inputs, outputs)


model = build_1dcnn_classifier(NUM_CLASSES, WINDOW_SIZE)
model.compile(optimizer="adam",
              loss="sparse_categorical_crossentropy",
              metrics=["accuracy"])

early_stop = tf.keras.callbacks.EarlyStopping(
    monitor="val_loss", patience=5, restore_best_weights=True
)

print(f"\nTraining on {len(X_train)} samples, validating on {len(X_test)}...")
model.fit(
    X_train, y_train,
    epochs=EPOCHS,
    batch_size=BATCH_SIZE,
    validation_data=(X_test, y_test),
    callbacks=[early_stop],
)

print("\nEvaluating model on held-out person...")
test_loss, test_acc = model.evaluate(X_test, y_test, verbose=0)
print(f"Test Accuracy ({TEST_PERSON} held out): {test_acc:.4f}")

preds = np.argmax(model.predict(X_test, verbose=0), axis=1)
print("\nPer-class accuracy on held-out set:")
for idx, lab in enumerate(unique_labels):
    mask = y_test == idx
    if mask.sum() == 0:
        continue
    acc = (preds[mask] == idx).mean()
    print(f"  {lab:<8s}  n={int(mask.sum()):4d}  acc={acc:.3f}")

# Confusion matrix: rows = true label, cols = predicted label
cm = np.zeros((NUM_CLASSES, NUM_CLASSES), dtype=np.int32)
for t, p in zip(y_test, preds):
    cm[t, p] += 1
print("\nConfusion matrix (rows=true, cols=predicted):")
print(" " * 10 + "".join(f"{lab[:7]:>8s}" for lab in unique_labels))
for i, lab in enumerate(unique_labels):
    row = "".join(f"{cm[i, j]:>8d}" for j in range(NUM_CLASSES))
    print(f"{lab[:9]:<9s} {row}")


# ---- Representative dataset for int8 quantization ----
def representative_dataset():
    num_samples = min(200, len(X_train))
    idx = np.random.default_rng(0).choice(len(X_train), size=num_samples, replace=False)
    for i in idx:
        yield [X_train[i:i + 1]]


converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_model = converter.convert()

tflite_path = f"classifier_3x{WINDOW_SIZE}x1_1dcnn_int8.tflite"
with open(tflite_path, "wb") as f:
    f.write(tflite_model)
print("Saved:", tflite_path)

labels_path = "classifier_labels.json"
with open(labels_path, "w") as f:
    json.dump({
        "labels": unique_labels,
        "num_classes": NUM_CLASSES,
        "window_size": WINDOW_SIZE,
        "normalization": "baked_into_model",
        "normalization_scale": NORMALIZATION_SCALE,
        "firmware_preprocessing": "none — pass raw accelerometer float values directly",
        "idle_label": IDLE_LABEL,
    }, f, indent=2)
print("Saved labels:", labels_path)

tf.lite.experimental.Analyzer.analyze(model_content=tflite_model)
print("Params:", model.count_params())
