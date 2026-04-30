import serial
import time
from datetime import datetime


class IndoorGPS:
    def __init__(self):
        self.initial_latlon = (59.37037, 10.44225)

        self.DWM = serial.Serial(port="COM6", baudrate=115200, timeout=1)

        self.pos_id = "0721"

        self.DWM.write("\r\r".encode())
        time.sleep(1)
        self.DWM.write("les\r".encode())
        time.sleep(1)

        print("Initialized Indoor GPS (position only)")

    def shutdown(self):
        print("Shutting down...")
        self.DWM.write("les\r".encode())
        self.DWM.close()

    def parse_line(self, line):
        line = line.strip()
        #print(f"RAW LINE: {repr(line)}")

        if "[" not in line or "]" not in line:
            return None

        left, right = line.split("[", 1)
        coords_str = right.split("]", 1)[0]

        left = left.strip()
        parts = left.split()

        if not parts:
            return None

        tag_id = parts[-1]   # gets "0721"
        if tag_id != self.pos_id:
            return None

        coords = coords_str.split(",")
        if len(coords) < 2:
            return None

        try:
            x_pos = float(coords[0])
            y_pos = float(coords[1])
        except ValueError:
            return None

        return {
            "x": x_pos,
            "y": y_pos,
            "timestamp": datetime.now(),
            "latlon_origin": self.initial_latlon
        }

    def read_data(self):
        try:
            line = self.DWM.readline().decode(errors="ignore").strip()
            #print(f"HEADER LINE: {repr(line)}")

            if not line:
                #print("Empty line")
                return None

            data = self.parse_line(line)
            if data is None:
                #print("No valid position found")
                return None

            return data

        except Exception as ex:
            print("ERROR:", ex)
            return None

    def run(self, rate_hz=10):
        interval = 1.0 / rate_hz

        try:
            while True:
                data = self.read_data()

                if data:
                    print(
                        f"Position [{data['timestamp']}] "
                        f"X: {data['x']:.2f}, Y: {data['y']:.2f}"
                    )

                time.sleep(interval)

        except KeyboardInterrupt:
            self.shutdown()


if __name__ == "__main__":
    gps = IndoorGPS()
    gps.run(rate_hz=10)