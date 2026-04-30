# Instructions

The Arduino_program.ino is the program that runs on the arduino. Commands are sent to this program to move the servos.
There are two options for using the system. One is based on the signal strength of the system, and the other is based on the divers' position.
For both options, the Arduino_program.ino has to be flashed and run on the arduino.

> [!WARNING]
> This system is a proof of concept.
> It has not been tested in water.

> [!NOTE]
> Change COM port in code to match your own.

## If using the system based on signal strength

If you are auto adjusting the transducer based on signal strength then the following scripts are applicable:
```
auto_adjuster.py
```

## If using the system based on position

If you are auto adjusting the transducer based on the divers position, then the following scripts are applicable:
```
auto_adjuster_pos.py
decawave.py
```

In this project we have used these Decawave positioning nodes: https://www.qorvo.com/products/p/MDEK1001.

> [!NOTE]
> During testing, a 10-20cm drift in X, Y and Z values of position has been observed.
