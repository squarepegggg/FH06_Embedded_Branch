const { contextBridge, ipcRenderer } = require('electron');

let predictionCallback = null;

ipcRenderer.on('prediction', (_event, pred) => {
  if (typeof predictionCallback === 'function') {
    predictionCallback(pred);
  }
});

contextBridge.exposeInMainWorld('electron', {
  setOnPrediction(fn) {
    predictionCallback = typeof fn === 'function' ? fn : null;
  },
});
