# Project Status Report

| Field | Value |
|-------|--------|
| **Date** | 12/5/25 |
| **Week Period** | From: 12/1/25  To: 12/5/25 |
| **Team Number** | FH06 |
| **Project Name** | Batteryless AI Device |
| **Mentor Name** | Radu Marculescu |
| **Sponsor / representative** | If any |

***

## Team / High-Level Overview of Work Initiated and/or Completed This Week

The team met on Monday, aligned on delegated tasks and a Wednesday deadline, and focused on delivering an integrated ML and embedded demo. We made progress on profiling, model design, data collection, sliding window prediction, and communication so the wearable is ready for batteryless, solar powered operation.

**Accomplishments:**

- **Profiling:** Measured inference latency, number of parameters, and peak memory for the on-device classifier.
- **Data collection:** Expanded and cleaned the wrist motion dataset and kept labeling and 25 Hz sampling consistent.
- **Idle class:** Combined standing and sitting into one idle label.
- **Sliding window prediction:** Implemented sliding 1 s windows (25 samples by 3 axes) for more frequent activity updates.
- **Communication:** Radio turns off after each broadcast until the next prediction is ready.
- **10 classes:** Defined a 10 class activity set and updated Edge Impulse and firmware labels.

***

## Work Initiated and/or Completed by Individual Team Members

| Name | UT EID | Task and Status |
|------|--------|------------------|
| **Karem Mohamed** | KM52839 | Implemented broadcast then turn off until next prediction and verified connection stability. |
| **Ronak Jain** | RJ23787 | Led data collection and defined the full 10 class set with consistent labeling and 25 Hz sampling. |
| **Andres Wearden** | AEW3364 | Implemented the sliding 1 s window in firmware and wired ML output and BLE to the new windowing. |
| **Nikhil Kabra** | NK23343 | Measured latency, parameters, and max memory and added the idle class (stand plus sit) in Edge Impulse. |
| **Matthew Olan** | MTO472 | Stabilized BMA400 SPI and FIFO path and prepared hooks for voltage monitoring and solar. |

***

## Team / High-Level Overview of Work to Be Initiated and/or Completed Next Week

**Theme: Make the wearable batteryless and solar powered.**

Move from bench power to solar powered, batteryless operation by integrating the solar panel and energy buffer, adding voltage monitoring (gatekeeper), and running the full pipeline on harvested energy only.

***

## Individual Upcoming Tasks (Next Week, Batteryless / Solar Focus)

| Name | UT EID | Upcoming Task |
|------|--------|----------------|
| **Karem Mohamed** | KM52839 | Extend broadcast then off logic to use the voltage gatekeeper and add ADC check and state definitions. |
| **Ronak Jain** | RJ23787 | Run 10 class validation under intermittent power and document edge cases when the device sleeps mid session. |
| **Andres Wearden** | AEW3364 | Add lightweight checkpointing so the sliding window and inference survive brownouts and resume correctly. |
| **Nikhil Kabra** | NK23343 | Re profile latency, parameters, and memory under solar duty cycle and tune model or window if needed. |
| **Matthew Olan** | MTO472 | Lead solar panel, PMIC, and capacitor integration and wire the gatekeeper into firmware; measure cold boot and energy per inference. |

***

## Notes from Weekly Meeting (Mentor / Industry Representative)

**Working:** Data collection works. Live inference is working and makes an inference every second.

**Profiling and next steps:** Profile latency, num parameters, and max memory. Continue data collection. Add an idle class by combining stand and sit. Implement sliding window prediction. Change communication so that when the device broadcasts it turns off until the next prediction. Come up with 10 classes.

***

## General Notes

- Profiling, data collection, idle class, sliding window prediction, broadcast then off communication, and the 10 class set are in scope; next week focuses on batteryless, solar powered operation with clear tasks per member.
