import serial
import time
import socket


class AutoAdjuster:
    def __init__(self, port="COM9", baud=115200, udp_port=4321, udp_ip="127.0.0.1"):
        self.port = port
        self.baud = baud
        self.udp_port = udp_port
        self.udp_ip = udp_ip
        self.ser = None

        self.signal_strength = 0
        self.low = 0.0037225226227928314

        # Initial sweep lists in degrees
        self.init_sweep_lat_list = [-10, 10, -20, 20, -30, 30, -40, 40, -50, 50]  # servo 2
        self.init_sweep_lon_list = [-90, -80, -70, -60, -50, -40, -30, -20, -10, 0, 10, 20, 30, 40, 50, 60, 70, 80, 90]  # servo 1

        # Store current servo positions in DEGREES
        self.cur_deg_servo1 = 0   # servo_id 1 (lon)
        self.cur_deg_servo2 = 0   # servo_id 2 (lat)

        # UDP socket
        self.udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_sock.bind((self.udp_ip, self.udp_port))
        self.udp_sock.settimeout(0.01)

        # Smooth motion tuning in degrees
        self.smooth_step_deg = 1
        self.smooth_step_delay = 0.0005

        self.smooth_step_deg_initial = 1
        self.smooth_step_delay_initial = 0.005

        self.initial_pos_found = False

    def connect(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1)
            time.sleep(2)
            print(f"Connected to {self.port} at {self.baud} baud.")
        except serial.SerialException as e:
            print(f"Error connecting to {self.port}: {e}")
            self.ser = None

    def close(self):
        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
                print("Serial connection closed.")
        finally:
            try:
                self.udp_sock.close()
            except Exception:
                pass

    def udp_poll_signal_strength(self):
        """Try to read newest UDP value without blocking."""
        try:
            data, _ = self.udp_sock.recvfrom(1024)
            self.signal_strength = float(data.decode(errors="ignore").strip())
            print(f"Received signal strength: {self.signal_strength}")
            return True
        except socket.timeout:
            return False
        except ValueError:
            return False

    def in_range(self) -> bool:
        return self.low <= self.signal_strength

    def clamp_deg(self, deg: int) -> int:
        return max(-90, min(90, deg))

    def deg_to_us(self, deg: int) -> int:
        """Convert -90..90 degrees to 600..2400 microseconds."""
        deg = self.clamp_deg(deg)
        pwm = 10 * deg + 1500
        return max(600, min(2400, pwm))

    def send_servo_command(self, servo_id: int, us: int):
        """Send raw microseconds directly."""
        if self.ser and self.ser.is_open:
            self.ser.write(f"{servo_id} {us}\n".encode())
            self.ser.flush()
            reply = self.ser.readline().decode(errors="ignore").strip()
            if reply:
                print("Arduino:", reply)
        else:
            print("Serial connection not established.")

    def send_servo_command_deg(self, servo_id: int, deg: int):
        """Send servo command using degrees."""
        us = self.deg_to_us(deg)
        self.send_servo_command(servo_id, us)

    def move_servo_smooth(
        self,
        servo_id: int,
        current_deg: int,
        target_deg: int,
        step_deg: int = 1,
        step_delay: float = 0.01
    ):
        """
        Smoothly move servo in degrees.
        Returns the last commanded degree.
        """
        current_deg = self.clamp_deg(current_deg)
        target_deg = self.clamp_deg(target_deg)

        if current_deg == target_deg:
            self.send_servo_command_deg(servo_id, target_deg)
            return target_deg

        direction = 1 if target_deg > current_deg else -1
        step_deg = abs(step_deg)

        deg = current_deg
        while (deg - target_deg) * direction < 0:
            deg += direction * step_deg

            if (deg - target_deg) * direction > 0:
                deg = target_deg

            self.send_servo_command_deg(servo_id, deg)
            time.sleep(step_delay)

            self.udp_poll_signal_strength()
            if self.in_range():
                break

        return deg

    def initial_sweep(self):
        print("Performing initial sweep...")

        self.initial_pos_found = False

        forward = True
        lon_forward = self.init_sweep_lon_list
        lon_reverse = list(reversed(self.init_sweep_lon_list))

        for deg_2 in self.init_sweep_lat_list:
            self.cur_deg_servo2 = self.move_servo_smooth(
                servo_id=2,
                current_deg=self.cur_deg_servo2,
                target_deg=deg_2,
                step_deg=self.smooth_step_deg_initial,
                step_delay=self.smooth_step_delay_initial,
            )

            lon_list = lon_forward if forward else lon_reverse
            forward = not forward

            for deg_1 in lon_list:
                self.cur_deg_servo1 = self.move_servo_smooth(
                    servo_id=1,
                    current_deg=self.cur_deg_servo1,
                    target_deg=deg_1,
                    step_deg=self.smooth_step_deg_initial,
                    step_delay=self.smooth_step_delay_initial,
                )

                print(f"Signal: {self.signal_strength}")

                if self.in_range():
                    print(
                        f"Found good position during initial sweep at "
                        f"servo1={self.cur_deg_servo1}deg, servo2={self.cur_deg_servo2}deg"
                    )
                    self.initial_pos_found = True
                    return
                
    def build_search_list(self, center: int, radius: int, step: int):
        values = []
        for offset in range(-radius, radius + 1, step):
            val = self.clamp_deg(center + offset)
            values.append(val)
        
        return sorted(set(values))
        

    def local_grid_search(self, lon_list: list, lat_list: list):

        for deg_2 in lat_list:       # servo 1 targets
            for deg_1 in lon_list:   # servo 2 targets
                self.cur_deg_servo1 = self.move_servo_smooth(
                    servo_id=1,
                    current_deg=self.cur_deg_servo1,
                    target_deg=deg_1,
                    step_deg=self.smooth_step_deg,
                    step_delay=self.smooth_step_delay,
                )

                self.cur_deg_servo2 = self.move_servo_smooth(
                    servo_id=2,
                    current_deg=self.cur_deg_servo2,
                    target_deg=deg_2,
                    step_deg=self.smooth_step_deg,
                    step_delay=self.smooth_step_delay,
                )

                print(
                    f"At servo1={self.cur_deg_servo1}deg, "
                    f"servo2={self.cur_deg_servo2}deg "
                    f"-> Signal: {self.signal_strength}"
                )

                if self.in_range():
                    print("Back in range. Holding position.")
                    return True
                
        return False

    def main(self):
        self.connect()
        if not self.ser:
            print("Failed to establish serial connection.")
            return

        # Startup in microseconds to known positions, but track in degrees for logic.
        self.send_servo_command(1, 600)
        self.send_servo_command(2, 1500)

        # internal tracking in degrees matching startup positions
        self.cur_deg_servo1 = -90   # 600us = -90 deg
        self.cur_deg_servo2 = 0     # 1500us = 0 deg

        print("Servos initialized with startup microsecond commands")
        time.sleep(1)

        self.initial_sweep()

        try:
            while True:

                if not self.initial_pos_found:
                    print("No good position found during initial sweep. Trying again...")
                    self.initial_sweep()
                    time.sleep(0.2)
                    continue


                self.udp_poll_signal_strength()
                print(f"Signal: {self.signal_strength}")

                if self.in_range():
                    time.sleep(0.2)
                    continue

                print(f"Out of range [{self.signal_strength}] -> scanning smoothly...")

                search_center_lon = self.cur_deg_servo1
                search_center_lat = self.cur_deg_servo2

                found = False

                for radius in [5, 10, 15, 20]:
                    print(f"Trying local grid search with radius {radius} deg")

                    deg_lon_list = self.build_search_list(search_center_lon, radius, step=5)
                    deg_lat_list = self.build_search_list(search_center_lat, radius, step=5)

                    found = self.local_grid_search(deg_lon_list, deg_lat_list)

                    if found:
                        break

                if not found:
                    print("Expanded grid search failed.")
                    self.initial_pos_found = False
                    time.sleep(0.2)


                time.sleep(0.2)

        except KeyboardInterrupt:
            print("Interrupted by user.")
            print(
                f"Last commanded -> servo1: {self.cur_deg_servo1}deg, "
                f"servo2: {self.cur_deg_servo2}deg"
            )
        finally:
            self.close()


if __name__ == "__main__":
    adjuster = AutoAdjuster()
    adjuster.main()