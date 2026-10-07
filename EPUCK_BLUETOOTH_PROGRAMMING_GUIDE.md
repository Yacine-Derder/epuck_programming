# E-Puck Bluetooth Programming Guide

A complete guide for programming e-puck robots via Bluetooth connection, from discovery through code deployment.

**Author:** Yacine Derder
**Date:** April 27, 2026

---

## Table of Contentsb

1. [Initial Bluetooth Discovery](#initial-bluetooth-discovery)
2. [Binding E-Pucks to RFCOMM Channels](#binding-e-pucks-to-rfcomm-channels)
3. [Code Compilation](#code-compilation)
4. [Uploading Code with epuckupload](#uploading-code-with-epuckupload)
5. [Serial Monitoring](#serial-monitoring)
6. [Available Examples](#available-examples)
7. [Troubleshooting: Old vs. New Makefiles](#troubleshooting-old-vs-new-makefiles)

---

## Initial Bluetooth Discovery

### Windows: Discovering E-Pucks

1. Open **Settings** → **Devices** → **Bluetooth & other devices**
2. Click **"Add a device"** or **"+ Bluetooth or other device"**
3. In the Bluetooth device list, look for **"Show all devices"** option (may appear at the bottom or as a link)
4. Search through the list for your e-puck device (typically appears as an unknown device or with a numerical identifier)
5. Once found, note the **Bluetooth MAC address** from the device properties

### Linux: Discovering E-Pucks

Use the following command to scan for Bluetooth devices:

```bash
bluetoothctl
```

Then in the bluetoothctl prompt, use:

```
scan on
```

This will display discovered devices with their MAC addresses. Look for your e-puck number in the device list. You can also use the command-line tool directly:

```bash
sudo hcitool scan
```

This displays a list of visible Bluetooth devices with their MAC addresses.

### Finding Your E-Puck

- E-pucks typically appear as **unknown devices** in discovery lists
- The MAC address format is: `XX:XX:XX:XX:XX:XX` (hexadecimal)
- Keep discovery active until you find your e-puck (may need to reset/power-cycle the robot)
- **Note the MAC address** for the next step

---

## Binding E-Pucks to RFCOMM Channels

Once you've discovered your e-puck's MAC address, you must bind it to an `/dev/rfcomm` device for serial communication.

### Using bind_epucks.py

The repository includes [bind_epucks.py](bind_epucks.py) which automates this process. Here's how to use it:

#### Step 1: Add Your E-Puck to the Script

Edit [bind_epucks.py](bind_epucks.py) and add your e-puck to the `EPUCKS` dictionary:

```python
EPUCKS = {
    99: "08:00:17:20:53:76",
    51: "08:00:17:20:6D:6C",
    YOUR_EPUCK_NUMBER: "YOUR:MAC:ADDRESS",  # Add your e-puck here
}
```

Replace `YOUR_EPUCK_NUMBER` with the e-puck's number and `YOUR:MAC:ADDRESS` with the MAC address you discovered above.

#### Step 2: Run the Binding Script

```bash
# Bind all configured e-pucks
sudo python3 bind_epucks.py

# Or bind specific e-pucks
sudo python3 bind_epucks.py 51 99

# List all configured e-pucks
python3 bind_epucks.py --list
```

**Note:** The script requires **root privileges** (`sudo`) to create `/dev/rfcomm` devices.

#### What the Binding Script Does

The `bind_epucks.py` script executes the following commands internally:

```bash
rfcomm bind /dev/rfcomm<EPUCK_NUMBER> <MAC_ADDRESS> 1
```

Where:

- `EPUCK_NUMBER` is the robot's identifier (e.g., 51)
- `MAC_ADDRESS` is the Bluetooth MAC address
- `1` is the RFCOMM channel

**Example for e-puck 51 with MAC `08:00:17:20:6D:6C`:**

```bash
sudo rfcomm bind /dev/rfcomm51 08:00:17:20:6D:6C 1
```

This creates a serial device at `/dev/rfcomm51` that represents the Bluetooth connection.

#### Unbinding E-Pucks

To release a binding:

```bash
sudo python3 bind_epucks.py --release 51

# Or manually:
sudo rfcomm release /dev/rfcomm51
```

---

## Code Compilation

### Understanding the E-Puck Folder Structure

The `epuck/` folder in this repository contains:

```
epuck/
├── Documentation/          # E-puck technical documentation and datasheets
├── EpuckDevelopmentTree/   # Main development environment
│   ├── library/            # Pre-compiled e-puck libraries
│   │   ├── a_d/            # Analog-to-Digital converter (accelerometer, proximity sensors)
│   │   ├── camera/         # Camera interface modules
│   │   ├── codec/          # Audio codec for speaker/microphone
│   │   ├── contrib/        # Additional modules (radio communication, robot ID)
│   │   ├── I2C/            # I2C protocol implementation
│   │   ├── motor_led/      # Motor control and LED drivers
│   │   ├── uart/           # Serial communication (UART)
│   │   └── std_microchip/  # Microchip PIC30 standard libraries
│   └── program/            # Example projects and tutorials
└── Tools/
    └── epuckupload         # Code upload utility
```

**Key Library Modules:**

- **motor_led/**: Motor speed control and LED status indicators
- **uart/**: Serial communication for logging and debugging
- **I2C/**: Communication with sensors (accelerometer, proximity sensors)
- **a_d/**: Analog-to-digital conversion for proximity sensors and accelerometer
- **contrib/radio_swis/**: Wireless inter-robot communication
- **contrib/robot_id/**: Robot identification system

### Building Code

Before using `epuckupload`, compile your code to generate the `.hex` file:

```bash
cd your_project_directory
make
```

This generates `your_program.hex` and `your_program.cof` files needed for uploading.

---

## Uploading Code with epuckupload

### Locating epuckupload

The `epuckupload` tool is located at:

```
epuck/Tools/epuckupload
```

Relative to the workspace root: `./epuck/Tools/epuckupload`

### Adding epuckupload to PATH

To use `epuckupload` from anywhere without specifying the full path:

```bash
7# Add to your PATH (temporary, for current session)
export PATH="$PATH:/path/to/epuck/Tools"

# Or create a symbolic link
sudo ln -s /path/to/epuck/Tools/epuckupload /usr/local/bin/epuckupload

# Or add to your shell configuration (~/.bashrc or ~/.zshrc) for permanent setup
echo 'export PATH="$PATH:/full/path/to/epuck/Tools"' >> ~/.bashrc
source ~/.bashrc
```

### Uploading Your Program

Ensure your e-puck is bound to `/dev/rfcomm<NUMBER>` (see [Binding E-Pucks](#binding-e-pucks-to-rfcomm-channels)):

```bash
epuckupload -f your_program.hex 51
```

Where `51` is your e-puck number. Replace with the appropriate number for your robot.

### Critical: Bluetooth PIN Entry

**When accessing the e-puck over Bluetooth for the first time, you may see a Bluetooth authentication prompt.**

**Important:** A Bluetooth window must be open on your desktop for the PIN request to appear. If you run `epuckupload` or other scripts from a headless terminal or without a Bluetooth manager window active, the PIN prompt may not display and the connection will hang. Ensure a Bluetooth application or system Bluetooth dialog is accessible before running any scripts that connect to the e-puck.

You **must** enter the e-puck's PIN before the connection proceeds. The PIN format is:

```
<EPUCK_NUMBER> padded with leading zeros to 4 digits
```

**Examples:**

- E-puck 51 → PIN: `0051`
- E-puck 99 → PIN: `0099`
- E-puck 5 → PIN: `0005`

**Important:** This PIN entry is typically required during the initial Bluetooth connection setup. Once a device is bound, subsequent connections from the same machine usually don't require re-authentication. However, if you see the PIN prompt, you must enter it correctly for the connection to succeed.

### Upload Procedure: Reset Button Timing

1. Run the `epuckupload` command
2. Enter the Bluetooth PIN when prompted
3. The terminal will display dots: `.....`
4. **When you see the dots, press the blue reset button on the e-puck**
5. If successful, you'll see asterisks: `*****`
6. The upload completes and your code runs on the e-puck

**Reference:** See the Lab03 assignment document (`Lab03/DIS_17-18_lab03_assignment.docx`) for more details.

---

## Serial Monitoring

Monitor serial output (printf, debugging messages) from your e-puck in real-time.

### Prerequisites

Ensure your e-puck is bound to an `/dev/rfcomm` device:

```bash
sudo python3 bind_epucks.py 51
```

### Option 1: Using minicom Directly

```bash
minicom -D /dev/rfcomm51 -b 115200
```

**Exit minicom:** Press `Ctrl+A`, then `X`

### Option 2: Using the monitor_epucks.py Script

The repository includes [monitor_epucks.py](monitor_epucks.py) for easier multi-robot monitoring:

```bash
# Monitor e-puck 51
python3 monitor_epucks.py 51

# Monitor multiple e-pucks simultaneously (opens new terminal windows)
python3 monitor_epucks.py 51 99

# List available monitoring tools
python3 monitor_epucks.py --help
```

The script:

- Automatically finds the correct `/dev/rfcomm` device
- Opens new terminal windows for each e-puck
- Supports minicom, picocom, or screen as backends
- **Note:** Ensure devices are already bound (using `bind_epucks.py`) before running the monitor

### Option 3: Using VS Code Serial Monitor Extension

For an integrated development experience, you can use the **Serial Monitor** extension in VS Code:

1. Install the "Serial Monitor" extension from the VS Code marketplace
2. Open the command palette (`Ctrl+Shift+P` or `Cmd+Shift+P`)
3. Search for "Serial Monitor: Open" and select your `/dev/rfcomm` device
4. Set baud rate to `115200`

This approach integrates serial monitoring directly into your editor, eliminating the need for separate terminal windows.

### Other Serial Monitors

```bash
# Using picocom
picocom -b 115200 /dev/rfcomm51

# Exit: Ctrl+A, then Ctrl+X

# Using screen
screen /dev/rfcomm51 115200

# Exit: Ctrl+A, then K (then confirm with 'y')
```

---

## Available Examples

This repository contains several example projects demonstrating e-puck programming:

### Lab03: Fundamental Robot Navigation

Located in `Lab03/Code/`:

- **obstacleavoidance/** - Proximity sensor-based obstacle avoidance
- **featurenavigation/** - Navigation using feature detection
- **objectfollowing/** - Following objects using camera or proximity
- **drnavigation/** - Dead-reckoning navigation
- **rulebased/** - Rule-based decision making

**Reference:** `Lab03/DIS_17-18_lab03_assignment.docx` - Assignment description and implementation notes

**Baseline Example:** The `sercom.hex` file (pre-compiled binary, source code unavailable) provides a good baseline for serial communication. It allows you to control motors, read all sensors, and toggle LEDs directly—useful for testing hardware functionality before developing your own code.

### Lab06: Collective Decision-Making

Located in `Lab06/Code/`:

- **collectivedecision/** - Multi-robot collective behavior using wireless communication
  - Example of inter-robot radio communication
  - Demonstrates consensus algorithms
- **webots/** - Simulation environments for testing collective algorithms

### Communication Module Example

Located in `comm_module/`:

- **comm_module.c** - Bare-metal communication example
- Uses the **newest Makefile structure** (see comparison below)
- Demonstrates radio communication and robot ID modules

---

## Compiling Your Own Code

When creating your own e-puck programs, base your project structure on the `comm_module/` example, which uses the correct, modern Makefile structure.

### Setup

1. **Copy the comm_module directory** as a template for your project:

   ```bash
   cp -r comm_module/ my_project/
   cd my_project/
   ```
2. **Modify the source code** (`my_project.c`, `my_project.h`, etc.) as needed
3. **Update the Makefile** if necessary to reflect your project name and any additional source files

### Building

**Clean the build artifacts** (removes `.o`, `.d`, `.cof`, and `.hex` files):

```bash
make clean
```

**Compile your code** (cross-compiles for the PIC30 microcontroller):

```bash
make
```

This generates:

- `my_project.hex` - Binary file for uploading to the e-puck
- `my_project.cof` - Debug symbol file
- `*.o`, `*.d` - Intermediate object and dependency files

### Upload and Test

After compilation, upload the `.hex` file:

```bash
epuckupload -f my_project.hex 51
```

Then monitor output:

```bash
python3 monitor_epucks.py 51
```

---

## Troubleshooting: Old vs. New Makefiles

### The Problem: Obsolete Makefiles

**Old Makefiles (e.g., `Lab03/Code/featurenavigation/Makefile`) no longer compile** due to:

1. **Linker command syntax changes** in newer `pic30-elf-gcc` toolchain versions
2. **Library ordering issues** - libraries must be wrapped with `--start-group` and `--end-group`
3. **Incorrect linker flags** - older flags are deprecated or incorrect

### Comparison: Old vs. New Makefile

#### OLD MAKEFILE (featurenavigation - BROKEN)

```makefile
# Line 68-71: Old linker definition
LD	 = pic30-elf-ld

# Line 73-77: Old linker flags (INCORRECT)
LDFLAGS  = -L$(EPUCKLIBROOT)/std_microchip/lib --defsym=__ICD2RAM=1 --script=$(EPUCKLIBROOT)/std_microchip/support/gld/p30f6014a.gld -mpic30_elf32
LDLIBS   = -lpic30-elf -lm-elf -lc-elf

# Line 88-89: Old linking command (BROKEN)
%.cof: $(OBJS)
	$(LD) $(LDFLAGS) --start-group $(LDLIBS) $(OBJS) $(EXTERNAL_OBJS) --end-group -o $@
```

**Problems:**

- Uses `pic30-elf-ld` directly (raw linker) instead of compiler driver
- Missing `-Wl,` wrapper for linker flags
- Incorrect library grouping syntax
- The `all:` rule includes an incorrectly indented debug rule

#### NEW MAKEFILE (comm_module - WORKING)

```makefile
# Line 62: NEW linker definition
LD	 = $(CC)

# Line 66-71: NEW linker flags (CORRECT)
LDFLAGS  = -mcpu=30F6014A -L$(EPUCKLIBROOT)/std_microchip/lib -Wl,--defsym=__ICD2RAM=1 -Wl,--script=$(EPUCKLIBROOT)/std_microchip/support/gld/p30f6014a.gld
LDLIBS   = -Wl,--start-group -lpic30-elf -lm-elf -lc-elf -Wl,--end-group

# Line 80-81: NEW linking command (CORRECT)
%.cof: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(EXTERNAL_OBJS) $(LDLIBS) -o $@
```

**Improvements:**

- Uses `$(CC)` (the compiler driver) instead of raw linker
- Wraps all linker flags with `-Wl,` for proper passing to linker
- Correctly groups libraries with `-Wl,--start-group` and `-Wl,--end-group`
- Cleaner and more maintainable syntax

### Why These Changes Are Necessary

1. **Compiler Driver (`pic30-elf-gcc`) vs Raw Linker (`pic30-elf-ld`)**

   - The compiler driver handles option translation and library paths better
   - Raw linker doesn't understand `-mcpu` and other compiler options
   - Modern toolchains expect the compiler driver
2. **`-Wl,` Prefix for Linker Flags**

   - Tells the compiler: "Pass this flag to the linker, don't process it"
   - Without `-Wl,`, the compiler tries to interpret linker flags as compiler options
   - Example: `-Wl,--start-group` tells compiler to pass `--start-group` to linker
3. **Library Grouping with `--start-group` and `--end-group`**

   - Circular dependencies between libraries require grouping
   - Without grouping, the linker may not resolve all symbols
   - The `--start-group` ... `--end-group` wrapper tells the linker to iterate through libraries multiple times

### Fixing Your Old Makefiles

Use the new Makefile pattern from `comm_module/Makefile`:

```diff
- LD	 = pic30-elf-ld
+ LD	 = $(CC)

- LDFLAGS  = -L$(EPUCKLIBROOT)/std_microchip/lib --defsym=__ICD2RAM=1 --script=$(EPUCKLIBROOT)/std_microchip/support/gld/p30f6014a.gld -mpic30_elf32
- LDLIBS   = -lpic30-elf -lm-elf -lc-elf
+ LDFLAGS  = -mcpu=30F6014A -L$(EPUCKLIBROOT)/std_microchip/lib -Wl,--defsym=__ICD2RAM=1 -Wl,--script=$(EPUCKLIBROOT)/std_microchip/support/gld/p30f6014a.gld
+ LDLIBS   = -Wl,--start-group -lpic30-elf -lm-elf -lc-elf -Wl,--end-group

- %.cof: $(OBJS)
- 	$(LD) $(LDFLAGS) --start-group $(LDLIBS) $(OBJS) $(EXTERNAL_OBJS) --end-group -o $@
+ %.cof: $(OBJS)
+ 	$(LD) $(LDFLAGS) $(OBJS) $(EXTERNAL_OBJS) $(LDLIBS) -o $@
```

**Also check the `all:` target** - the new version should look like:

```makefile
all: $(PROG).hex $(DEPS)
```

Not:

```makefile
all: $(PROG).hex tags $(DEPS) 

.PHONY: debug
	debug: all  # Wrong indentation!
```

---

## Quick Reference

### Complete Upload Workflow

```bash
# 1. Add e-puck to bind_epucks.py EPUCKS dictionary
# 2. Bind the e-puck
sudo python3 bind_epucks.py 51

# 3. Compile your code (from your project directory)
cd Lab03/Code/myproject
make

# 4. Upload (enter PIN when prompted)
epuckupload /dev/rfcomm51 myproject.hex
# → Press blue reset button when you see .....
# → Should see *****

# 5. Monitor output
python3 ../../monitor_epucks.py 51
# Or: minicom -D /dev/rfcomm51 -b 115200
```

### PIN Reference Table

| E-Puck # | PIN  | Bound Device   |
| -------- | ---- | -------------- |
| 5        | 0005 | /dev/rfcomm5   |
| 51       | 0051 | /dev/rfcomm51  |
| 99       | 0099 | /dev/rfcomm99  |
| 100      | 0100 | /dev/rfcomm100 |

---

## Additional Resources

- **E-Puck Official Documentation**: See `epuck/Documentation/`
- **Development Tree**: `epuck/EpuckDevelopmentTree/`
- **Lab Assignments**: `Lab03/` and `Lab06/` contain detailed debriefing documents
- **Python Scripts**: [bind_epucks.py](bind_epucks.py) and [monitor_epucks.py](monitor_epucks.py)
