import serial
import time
from datetime import datetime
import numpy as np
import socket

class DecawaveGPS:
    def __init__(self, udp_port=65000, udp_ip="127.0.0.1"):

        self.DWM = serial.Serial(port="COM6", baudrate=115200, timeout=1)

        self.udp_port = udp_port
        self.udp_ip = udp_ip

        self.diver_node_id = "0721"
        self.adjuster_node_id = "DC8D"
        self.diver_pos = np.array([0.0, 0.0, 0.0], dtype=float)
        self.adjuster_pos = np.array([0.0, 0.0, 0.0], dtype=float)
        self.vector = np.array([0.0, 0.0, 0.0], dtype=float)

        # Track min/max over runtime
        self.diver_min = None
        self.diver_max = None
        self.adjuster_min = None
        self.adjuster_max = None
        self.vector_min = None
        self.vector_max = None

        self.DWM.write("\r\r".encode())
        time.sleep(1)
        self.DWM.write("les\r".encode())
        time.sleep(1)

        print("Initialized Indoor GPS (position only)")

    def update_range(self, value, current_min, current_max):
        if current_min is None:
            return value.copy(), value.copy()
        return np.minimum(current_min, value), np.maximum(current_max, value)

    def print_runtime_difference(self):
        print("\n========== Runtime position differences ==========")

        if self.diver_min is not None and self.diver_max is not None:
            diver_diff = self.diver_max - self.diver_min
            print("Diver position spread:")
            print(f"  X diff: {diver_diff[0]:.4f}")
            print(f"  Y diff: {diver_diff[1]:.4f}")
            print(f"  Z diff: {diver_diff[2]:.4f}")
        else:
            print("Diver position spread: No valid data received.")

        if self.adjuster_min is not None and self.adjuster_max is not None:
            adjuster_diff = self.adjuster_max - self.adjuster_min
            print("Adjuster position spread:")
            print(f"  X diff: {adjuster_diff[0]:.4f}")
            print(f"  Y diff: {adjuster_diff[1]:.4f}")
            print(f"  Z diff: {adjuster_diff[2]:.4f}")
        else:
            print("Adjuster position spread: No valid data received.")

        if self.vector_min is not None and self.vector_max is not None:
            vector_diff = self.vector_max - self.vector_min
            print("Vector spread (diver - adjuster):")
            print(f"  X diff: {vector_diff[0]:.4f}")
            print(f"  Y diff: {vector_diff[1]:.4f}")
            print(f"  Z diff: {vector_diff[2]:.4f}")
        else:
            print("Vector spread: No valid data received.")

        print("===============================================\n")

    def send_udp_message(self, target_ip: str, target_port: int):
        udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        udp_message = [self.adjuster_pos, self.diver_pos]

        data = str(udp_message).encode('utf-8')
        udp_socket.sendto(data, (target_ip, target_port))

        udp_socket.close()

    def shutdown(self):
        print("Shutting down...")
        self.print_runtime_difference()
        self.DWM.write("les\r".encode())
        self.DWM.close()

    def parse_line(self, line):
        line = line.strip()

        if "[" not in line or "]" not in line:
            return None

        left, right = line.split("[", 1)
        coords_str = right.split("]", 1)[0]

        left = left.strip()
        parts = left.split()

        if not parts:
            return None

        tag_id = parts[-1]

        coords = coords_str.split(",")
        if len(coords) < 3:
            return None

        parsed_pos = np.array([float(c) for c in coords[:3]], dtype=float)

        if tag_id == self.diver_node_id:
            self.diver_pos = parsed_pos
            self.diver_min, self.diver_max = self.update_range(
                self.diver_pos, self.diver_min, self.diver_max
            )

        elif tag_id == self.adjuster_node_id:
            self.adjuster_pos = parsed_pos
            self.adjuster_min, self.adjuster_max = self.update_range(
                self.adjuster_pos, self.adjuster_min, self.adjuster_max
            )
        else:
            return None

        self.vector = self.diver_pos - self.adjuster_pos
        self.vector_min, self.vector_max = self.update_range(
            self.vector, self.vector_min, self.vector_max
        )

        print(f"Vector from adjuster to diver: {self.vector}")

        return {
            "diver_pos": self.diver_pos,
            "adjuster_pos": self.adjuster_pos,
        }

    def read_data(self):
        try:
            line = self.DWM.readline().decode(errors="ignore").strip()

            if not line:
                return None

            data = self.parse_line(line)
            if data is None:
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
                        f"Position diver!!!! [{data['diver_pos']}] "
                        f"X: {data['diver_pos'][0]:.2f}, Y: {data['diver_pos'][1]:.2f}, Z: {data['diver_pos'][2]:.2f}"
                    )
                    print(
                        f"Position adjuster!!!! [{data['adjuster_pos']}] "
                        f"X: {data['adjuster_pos'][0]:.2f}, Y: {data['adjuster_pos'][1]:.2f}, Z: {data['adjuster_pos'][2]:.2f}"
                    )

                    self.send_udp_message(self.udp_ip, self.udp_port)

                time.sleep(interval)

        except KeyboardInterrupt:
            self.shutdown()


if __name__ == "__main__":
    gps = DecawaveGPS()
    gps.run(rate_hz=10)