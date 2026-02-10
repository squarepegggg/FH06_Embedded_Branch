import tensorflow as tf
import numpy as np
import pandas as pd
import glob
import json
import os
from pathlib import Path

print("TensorFlow:", tf.__version__)

# Load CSV files and extract labels
def load_csv_data(csv_dir="."):
    """Load all CSV files and extract labels from filenames."""
    csv_files = glob.glob(os.path.join(csv_dir, "*.csv"))
    all_data = []
    all_labels = []  # One label per file
    
    for csv_file in csv_files:
        # Extract label from filename: "Andres_sitting.csv" -> "sitting"
        filename = Path(csv_file).stem  # Gets "Andres_sitting" without .csv
        # Find the first underscore and take everything after it
        if "_" in filename:
            label = filename.split("_", 1)[1]  # Split on first underscore, take second part
        else:
            label = filename  # Fallback if no underscore
        
        # Load CSV data
        df = pd.read_csv(csv_file)
        # Extract X, Y, Z columns (skip Timestamp)
        data = df[["X", "Y", "Z"]].values.astype(np.float32)
        
        all_data.append(data)
        all_labels.append(label)  # One label per file
    
    return all_data, all_labels

# Create windows of size 25 from the time series data
def create_windows(data_list, labels_list, window_size=25):
    """Create sliding windows of size window_size from time series data."""
    X_windows = []
    y_windows = []
    
    for data, label in zip(data_list, labels_list):
        if len(data) < window_size:
            continue  # Skip if not enough data
        
        # Create sliding windows
        for i in range(len(data) - window_size + 1):
            window = data[i:i+window_size]  # Shape: (25, 3)
            # Reshape to (3, 25, 1) as expected by the model
            window_reshaped = window.T.reshape(3, 25, 1)  # Transpose to (3, 25) then add channel dim
            X_windows.append(window_reshaped)
            # All windows from this file have the same label
            y_windows.append(label)
    
    return np.array(X_windows), np.array(y_windows)

# Load data from CSV files
print("Loading CSV files...")
data_list, labels_list = load_csv_data()

# Create windows
print("Creating windows...")
X, y_strings = create_windows(data_list, labels_list, window_size=25)

# Map string labels to integers
unique_labels = sorted(list(set(y_strings)))
label_to_int = {label: idx for idx, label in enumerate(unique_labels)}
int_to_label = {idx: label for label, idx in label_to_int.items()}

y = np.array([label_to_int[label] for label in y_strings], dtype=np.int32)

NUM_CLASSES = len(unique_labels)
# unique_labels is sorted, so class index i = unique_labels[i]
print(f"Found {NUM_CLASSES} classes: {unique_labels}")
print(f"Total samples: {len(X)}")
print("Class index -> label (use this for inference):")
for idx, label in enumerate(unique_labels):
    print(f"  {idx} -> {label}")
print(f"(Full mapping: {label_to_int})")

def build_1dcnn_classifier(num_classes: int):
    inputs = tf.keras.Input(shape=(3, 25, 1), name="input_3x25x1")

    # Conv1D expects (length, channels). Treat 3x25 as a length-75 sequence.
    x = tf.keras.layers.Reshape((75, 1), name="reshape_to_75x1")(inputs)

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

model = build_1dcnn_classifier(NUM_CLASSES)
model.compile(optimizer="adam",
              loss="sparse_categorical_crossentropy",
              metrics=["accuracy"])
# Use a validation split to separate training and testing data
print(f"\nTraining on {len(X)} samples...")
from sklearn.model_selection import train_test_split
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.2, random_state=42, stratify=y
)

model.fit(X_train, y_train, epochs=3, batch_size=32, validation_data=(X_test, y_test))

# ---- Evaluate the model on the test set ----
print("\nEvaluating model on test data...")
test_loss, test_acc = model.evaluate(X_test, y_test, verbose=0)
print(f"Test Accuracy: {test_acc:.4f}")

# ---- Representative dataset for int8 quantization ----
def representative_dataset():
    # Use real samples from your training/validation set here.
    num_samples = min(100, len(X))
    for i in range(num_samples):
        # yield a list of input tensors matching model inputs
        sample = X[i:i+1]  # shape (1,3,25,1), float32
        yield [sample]

# ---- Convert to fully-int8 TFLite (TFLite Micro friendly) ----
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset

# Force built-in INT8 kernels only (best for Edge Impulse / TFLite Micro)
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]

# Full integer I/O (often preferred for MCUs)
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_model = converter.convert()

tflite_path = "classifier_3x25x1_1dcnn_int8.tflite"
with open(tflite_path, "wb") as f:
    f.write(tflite_model)

print("Saved:", tflite_path)

# Save class index -> label so you know which output index = which activity
labels_path = "classifier_labels.json"
with open(labels_path, "w") as f:
    json.dump({"labels": unique_labels, "num_classes": NUM_CLASSES}, f, indent=2)
print("Saved labels:", labels_path, "-> index i means label", unique_labels)

# ---- Inspect ops (helpful if EI still complains) ----
tf.lite.experimental.Analyzer.analyze(model_content=tflite_model)
print(model.count_params())