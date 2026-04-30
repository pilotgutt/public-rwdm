import serial
import time
import socket
import numpy as np


class AutoAdjuster:
    def __init__(self, port="COM9", baud=115200, udp_port=65000, udp_ip="0.0.0.0"):
        self.port = port
        self.baud = baud
        self.udp_port = udp_port
        self.udp_ip = udp_ip
        self.ser = None

        # Store current servo positions in DEGREES
        self.cur_deg_servo1 = 0   # servo_id 1 (lon)
        self.cur_deg_servo2 = 0   # servo_id 2 (lat)

        # UDP socket
        self.udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_sock.bind((self.udp_ip, self.udp_port))
        self.udp_sock.settimeout(20.0)

        # Smooth motion tuning in degrees
        self.smooth_step_deg = 1
        self.smooth_step_delay = 0.0005

        self.smooth_step_deg_initial = 1
        self.smooth_step_delay_initial = 0.005

        self.diver_pos = np.array([0.0, 0.0, 0.0])
        self.adjuster_pos = np.array([0.0, 0.0, 0.0])
        self.adjuster_2d_pos = np.array([0.0, 0.0])
        self.diver_2d_pos = np.array([0.0, 0.0])

        self.lon_unit_vec = np.array([0.0, 1.0, 0.0])
        self.lat_unit_vec = np.array([0.0, 0.0, 1.0])


    # def connect(self):
    #     try:
    #         self.ser = serial.Serial(self.port, self.baud, timeout=1)
    #         time.sleep(2)
    #         print(f"Connected to {self.port} at {self.baud} baud.")
    #     except serial.SerialException as e:
    #         print(f"Error connecting to {self.port}: {e}")
    #         self.ser = None

    # def close(self):
    #     try:
    #         if self.ser and self.ser.is_open:
    #             self.ser.close()
    #             print("Serial connection closed.")
    #     finally:
    #         try:
    #             self.udp_sock.close()
    #         except Exception:
    #             pass


    # def clamp_deg(self, deg: int) -> int:
    #     return max(-90, min(90, deg))

    # def deg_to_us(self, deg: int) -> int:
    #     """Convert -90..90 degrees to 600..2400 microseconds."""
    #     deg = self.clamp_deg(deg)
    #     pwm = 10 * deg + 1500
    #     return max(600, min(2400, pwm))

    # def send_servo_command(self, servo_id: int, us: int):
    #     """Send raw microseconds directly."""
    #     if self.ser and self.ser.is_open:
    #         self.ser.write(f"{servo_id} {us}\n".encode())
    #         self.ser.flush()
    #         reply = self.ser.readline().decode(errors="ignore").strip()
    #         if reply:
    #             print("Arduino:", reply)
    #     else:
    #         print("Serial connection not established.")

    # def send_servo_command_deg(self, servo_id: int, deg: int):
    #     """Send servo command using degrees."""
    #     us = self.deg_to_us(deg)
    #     self.send_servo_command(servo_id, us)

    # def move_servo_smooth(
    #     self,
    #     servo_id: int,
    #     current_deg: int,
    #     target_deg: int,
    #     step_deg: int = 1,
    #     step_delay: float = 0.01
    # ):
    #     """
    #     Smoothly move servo in degrees.
    #     Returns the last commanded degree.
    #     """
    #     current_deg = self.clamp_deg(current_deg)
    #     target_deg = self.clamp_deg(target_deg)

    #     if current_deg == target_deg:
    #         self.send_servo_command_deg(servo_id, target_deg)
    #         return target_deg

    #     direction = 1 if target_deg > current_deg else -1
    #     step_deg = abs(step_deg)

    #     deg = current_deg
    #     while (deg - target_deg) * direction < 0:
    #         deg += direction * step_deg

    #         if (deg - target_deg) * direction > 0:
    #             deg = target_deg

    #         self.send_servo_command_deg(servo_id, deg)
    #         time.sleep(step_delay)

    #     return deg

    
    def angle_calc(self, v1, v2):
        """Calculate angle in degrees between two vectors."""
        v1_len = np.linalg.norm(v1)
        v2_len = np.linalg.norm(v2)
        
        dot_prod = np.dot(v1, v2)

        cos_angle = dot_prod / (v1_len * v2_len)
        cos_angle = np.clip(cos_angle, -1.0, 1.0)

        angle_deg = np.degrees(np.arccos(cos_angle))

        return angle_deg    
        
        # v1_u = v1 / np.linalg.norm(v1)
        # v2_u = v2 / np.linalg.norm(v2)
        # dot_prod = np.clip(np.dot(v1_u, v2_u), -1.0, 1.0)
        # angle_rad = np.arccos(dot_prod)
        # return np.degrees(angle_rad)

    def angle_2d(self, v1, v2):
        angle1 = np.arctan2(v1[1], v1[0])
        angle2 = np.arctan2(v2[1], v2[0])
        angle = angle2 - angle1
        return np.degrees(angle)
    
    def calculate_servo_angles(self, adjuster_pos, diver_pos):
        """Caculate lon servo angle"""
        adjuster_pos_2d = np.array([adjuster_pos[0], adjuster_pos[1]])
        lon_unit_point = np.array([adjuster_pos[0], adjuster_pos[1] +1])
        #print(f"adjuster_pos_2d: {adjuster_pos_2d}")
        #print(f"lon_unit_point: {lon_unit_point}")
        adjuster_unit_vec = lon_unit_point - adjuster_pos_2d

        diver_pos_2d = np.array([diver_pos[0], diver_pos[1]])
        diver_vec = diver_pos_2d - adjuster_pos_2d
        #print(f"diver_pos_2d: {diver_pos_2d}")

        lon_angle = self.angle_2d(adjuster_unit_vec, diver_vec)
        print(f"lon_angle FROM CALC: {lon_angle}")

        lon_servo_deg = round(lon_angle + 180)
        print(f"Fucking lon_servo_deg: {lon_servo_deg} FROM CALC")

        """Calculate lat servo angle"""
        lat_unit_point = np.array([adjuster_pos[0], adjuster_pos[1], adjuster_pos[2] -5])
        lat_unit_vec = lat_unit_point - adjuster_pos
        diver_vec_3d = diver_pos - adjuster_pos

        lat_angle = self.angle_calc(lat_unit_vec, diver_vec_3d)
        print(f"lat_angle FROM CALC: {lat_angle}")
        lat_servo_deg = round(90 - lat_angle)

        return lon_servo_deg, lat_servo_deg


    def main(self):
        # self.connect()
        # if not self.ser:
        #     print("Failed to establish serial connection.")
        #     return

        # # Startup in microseconds to known positions, but track in degrees for logic.
        # self.send_servo_command(1, 1500)
        # self.send_servo_command(2, 1500)

        # 600us = -90 deg
        # 1500us = 0 deg
        # internal tracking in degrees matching startup positions
        self.cur_deg_servo1 = 0  
        self.cur_deg_servo2 = 0    

        print("Servos initialized with startup microsecond commands")
        time.sleep(1)


        try:
            while True:
                removedstr = ["[array([", "])", "array([", "])]", "]", " "]
                data, addr = self.udp_sock.recvfrom(1024)
                if data:
                    #print(f"Received UDP message from {addr}: {data}")
                    for rstr in removedstr:
                        data = data.replace(rstr.encode(), b"")
                    # print(f"Raw data: {data.decode(errors='ignore').split(',')[0]}")
                    ndata = [float(x) for x in data.decode(errors='ignore').split(',')]

                    # check if any values are negative, if so set to 0
                    #ndata = [0 if x < 0 else x for x in ndata]

                    #self.adjuster_pos = np.array([ndata[0], ndata[1], ndata[2]])
                    self.adjuster_pos = np.array([0.0, 0.0, 0.0])
                    #print(f"Parsed adjuster position: {self.adjuster_pos}")

                    self.diver_pos = np.array([ndata[0], ndata[1], -2.31])
                    #print(f"Parsed diver position: {self.diver_pos}")

                    lon_servo_deg, lat_servo_deg = self.calculate_servo_angles(self.adjuster_pos, self.diver_pos)

                    print(f"lon_servo_deg: {lon_servo_deg}")
                    print(f"lat_servo_deg: {lat_servo_deg}")

                    # self.send_servo_command_deg(1, int(lon_servo_deg))
                    # self.send_servo_command_deg(2, int(lat_servo_deg))




      
                    

        except KeyboardInterrupt:
            print("Interrupted by user.")
            print(
                f"Last commanded -> servo1: {self.cur_deg_servo1}deg, "
                f"servo2: {self.cur_deg_servo2}deg"
            )
        # finally:
        #     self.close()


if __name__ == "__main__":
    adjuster = AutoAdjuster()
    adjuster.main()