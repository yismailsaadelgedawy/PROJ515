#!/usr/bin/env python3
import time
import json
import spidev
import os
import subprocess
import sys
from pathlib import Path
from datetime import datetime
import math
#import pigpio
import select 

# Import Firebase utility functions
from upload_utils import update_hive_data, send_alert, check_threshold_and_alert, MaintenanceHandler

# Firebase identifiers
USER_ID = "CO3Mde5N9qbJpS9TwJslvABpAOk2" 
HIVE_ID = "kjiqHt0pbaIMvAUFArp9"

# GPIO pin configuration
MAINTENANCE_SWITCH_PIN = 24  # GPIO pin for maintenance switch
LED_PIN = 27                # GPIO pin for green LED

# SPI setup
spi = None  # Will be initialized in main()

# Configuration
ACTIVITY_LOG_PATH = "activity_log.json"
HEALTH_LOG_PATH = "health_log.json"
VERIFICATION_LOG_PATH = "verification_log.json"
UPDATE_INTERVAL = 60  # seconds between Firebase updates
WASP_VERIFICATION_CAMERA = "2"  # Second camera for wasp verification
WASP_VERIFICATION_DURATION = 15  # seconds to run the verification camera

class HiveMonitor:
    def __init__(self, activity_log_path=ACTIVITY_LOG_PATH, health_log_path=HEALTH_LOG_PATH, verification_log_path=VERIFICATION_LOG_PATH):
        """Initialize the hive monitoring system"""
        self.activity_log_path = activity_log_path
        self.health_log_path = health_log_path
        self.verification_log_path = verification_log_path
        
        # Ensure log files exist
        self._ensure_log_file(activity_log_path)
        self._ensure_log_file(health_log_path)
        self._ensure_log_file(verification_log_path)
        
        # Current state
        self.temperature = 0
        self.battery = 0
        self.raw_swarm_likelihood = 0
        self.brood_state = 0
        self.activity_level = 0
        self.health_score = 0
        self.swarm_likelihood = 0
        
        # Wasp detection
        self.wasp_detected = False
        self.wasp_verified = False
        self.verification_in_progress = False
        
        # Timestamps
        self.last_update_time = 0
        self.last_activity_data_time = 0
    
    def _ensure_log_file(self, file_path):
        """Create log file if it doesn't exist"""
        if not os.path.exists(file_path):
            directory = os.path.dirname(file_path)
            if directory and not os.path.exists(directory):
                os.makedirs(directory)
            with open(file_path, 'w') as f:
                json.dump({"records": []}, f)
            print(f"Created new log file: {file_path}")
    
    def read_mcu_data(self):
        """Read data from MCU via SPI"""
        # If SPI failed to initialize, return simulated data
        if spi is None:
            return self._get_simulated_data()
        
        try:
            response = spi.xfer2([0x00, 0x00, 0x00, 0x00])
            if len(response) == 4:
                return response
            else:
                print("Invalid response length:", response)
                return self._get_simulated_data()
        except Exception as e:
            print(f"SPI read failed: {e}")
            return self._get_simulated_data()
    
    def _get_simulated_data(self):
        """Generate simulated data for testing when SPI fails"""
        import random
        # Simulate temperature (32-36°C), battery (0-100%), swarm likelihood (0-255), brood state (0-5)
        temp = int(random.uniform(32, 36) * 2)  # Scale up for the protocol
        battery = int(random.uniform(60, 95))
        swarm = int(random.uniform(10, 200))
        brood = int(random.uniform(0, 5))
        
        print("Using simulated data for testing")
        return [temp, battery, swarm, brood]
    
    def interpret_mcu_data(self, data):
        """Convert raw byte values to real-world data"""
        a, b, c, d = data
        self.temperature = int(round(a / 2.0, 2))       # e.g. 204 → 102.0 °C
        self.battery = int(round((b / 255.0) * 100, 1)) # e.g. 128 → 50.2%
        self.raw_swarm_likelihood = int(c)              # keep as 0–255 scale for now
        self.brood_state = int(d)                       # 0–N mapped in frontend/backend
        
        return self.temperature, self.battery, self.raw_swarm_likelihood, self.brood_state
    
    def run_wasp_verification(self):
        """
        Run a secondary camera check to verify wasp detection
        
        This function:
        1. Executes the bee_monitor.py script with a second camera
        2. Runs for a shorter duration (15 seconds)
        3. Checks the resulting log for wasp detections
        """
        if self.verification_in_progress:
            print("Wasp verification already in progress")
            return False
            
        print("\n⚠️ WASP DETECTED! Running secondary camera verification...")
        self.verification_in_progress = True
        
        # Create a timestamp for this verification run
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        verification_dir = f"wasp_verification_{timestamp}"
        
        # Prepare command
        cmd = [
            sys.executable, 
            "bee_monitor.py",
            "--source", WASP_VERIFICATION_CAMERA,
            "--record-length", str(WASP_VERIFICATION_DURATION),
            "--json-path", self.verification_log_path,
            "--project", verification_dir,
            "--name", "wasp_verification"
        ]
        
        try:
            # Run the verification camera
            print(f"Running verification camera: {' '.join(cmd)}")
            process = subprocess.run(cmd, timeout=WASP_VERIFICATION_DURATION + 10)
            
            # Check if verification was successful
            if process.returncode == 0:
                # Read verification log file to check for wasps
                wasps_verified = self.check_verification_log()
                
                if wasps_verified:
                    print("⚠️⚠️ WASP DETECTION CONFIRMED on second camera!")
                    self.wasp_verified = True
                    return True
                else:
                    print("✓ No wasps detected on second camera - likely a false positive")
                    self.wasp_verified = False
                    return False
            else:
                print(f"Verification process failed with return code {process.returncode}")
                return False
                
        except subprocess.TimeoutExpired:
            print("Verification process timed out")
            return False
        except Exception as e:
            print(f"Error during wasp verification: {e}")
            return False
        finally:
            self.verification_in_progress = False
    
    def check_verification_log(self):
        """Check the verification log file for wasp detections"""
        try:
            with open(self.verification_log_path, 'r') as f:
                data = json.load(f)
                
            # Look for wasp detections in verification data
            if "activity_records" in data:
                for record in data["activity_records"]:
                    # Check if the record contains wasp detection info
                    if "wasps_detected" in record and record["wasps_detected"] > 0:
                        return True
                        
            return False
            
        except (json.JSONDecodeError, FileNotFoundError) as e:
            print(f"Error reading verification log: {e}")
            return False
            
    def read_latest_activity(self):
        """Read the latest activity data from JSON file"""
        try:
            if not os.path.exists(self.activity_log_path):
                return 0
                
            with open(self.activity_log_path, 'r') as f:
                data = json.load(f)
            
            if not data or "activity_records" not in data or not data["activity_records"]:
                return 0
            
            # Get the latest activity record
            latest_record = data["activity_records"][-1]
            self.activity_level = latest_record.get("activity_level", 0)
            self.last_activity_data_time = datetime.fromisoformat(latest_record.get("timestamp", datetime.now().isoformat()))
            
            # Check for wasp detection
            if "wasps_detected" in latest_record and latest_record["wasps_detected"] > 0:
                print(f"Wasps detected in activity log: {latest_record['wasps_detected']}")
                self.wasp_detected = True
            else:
                self.wasp_detected = False
            
            return self.activity_level
        
        except (json.JSONDecodeError, FileNotFoundError, KeyError) as e:
            print(f"Error reading activity data: {e}")
            return 0

    def calculate_health_score(self):
        """
        Calculate a health score (0-100) based on:
        - Temperature (optimal range: 32-36°C)
        - Activity level
        - Battery level
        """
        # Get latest activity data
        activity = self.read_latest_activity()
        
        # Temperature factor (0-40 points)
        # Optimal hive temperature is around 34-35°C
        # Score decreases as temperature deviates from optimal
        temp = self.temperature
        if 34 <= temp <= 35:
            temp_score = 40  # Optimal
        elif 32 <= temp < 34 or 35 < temp <= 36:
            temp_score = 35  # Good
        elif 30 <= temp < 32 or 36 < temp <= 38:
            temp_score = 25  # Concerning
        elif 25 <= temp < 30 or 38 < temp <= 40:
            temp_score = 15  # Problematic
        else:
            temp_score = 5   # Critical
        
        # Activity factor (0-40 points)
        # Scale directly from activity level, but with diminishing returns above 80
        if activity > 80:
            activity_score = 40
        else:
            activity_score = (activity / 80) * 40
        
        # Battery health factor (0-20 points)
        # Scale directly from battery percentage
        battery_score = (self.battery / 100) * 20
        
        # Calculate overall health score
        self.health_score = int(temp_score + activity_score + battery_score)
        
        # Ensure it's in range 0-100
        self.health_score = max(0, min(100, self.health_score))
        
        return self.health_score
    
    def calculate_swarm_likelihood(self):
        """
        Calculate swarm likelihood (0-100) based on:
        - Temperature
        - Activity level
        - Time of year (season)
        - Brood state
        - Raw swarm likelihood from sensor
        """
        # Get latest activity data
        activity = self.read_latest_activity()
        
        # 1. Season factor (0-30 points)
        # Bees swarm most often in late spring to early summer
        current_month = datetime.now().month
        if 4 <= current_month <= 6:  # April-June: peak swarm season
            season_factor = 30
        elif current_month in [3, 7]:  # March, July: moderate risk
            season_factor = 20
        elif current_month in [2, 8]:  # February, August: low risk
            season_factor = 10
        else:  # Other months: minimal risk
            season_factor = 0
        
        # 2. Temperature factor (0-25 points)
        # Bees are more likely to swarm when it's warm
        temp = self.temperature
        if temp >= 36:
            temp_factor = 25  # Hot - high swarm risk
        elif 34 <= temp < 36:
            temp_factor = 20  # Warm - elevated risk
        elif 30 <= temp < 34:
            temp_factor = 15  # Normal - moderate risk
        elif 25 <= temp < 30:
            temp_factor = 10  # Cool - low risk
        else:
            temp_factor = 0   # Cold - minimal risk
        
        # 3. Activity factor (0-25 points)
        # High activity can indicate swarming preparations
        if activity >= 85:
            activity_factor = 25  # Very high activity
        elif 70 <= activity < 85:
            activity_factor = 20  # High activity
        elif 50 <= activity < 70:
            activity_factor = 15  # Moderate activity
        elif 30 <= activity < 50:
            activity_factor = 10  # Low activity
        else:
            activity_factor = 5   # Very low activity
        
        # 4. Raw sensor data factor (0-20 points)
        # Scale the raw swarm likelihood (0-255) to 0-20 points
        sensor_factor = (self.raw_swarm_likelihood / 255) * 20
        
        # Calculate overall swarm likelihood
        self.swarm_likelihood = int(season_factor + temp_factor + activity_factor + sensor_factor)
        
        # Ensure it's in range 0-100
        self.swarm_likelihood = max(0, min(100, self.swarm_likelihood))
        
        return self.swarm_likelihood
    
    def save_health_data(self):
        """Save health and swarm data to JSON log"""
        timestamp = datetime.now().isoformat()
        
        # Read existing data
        try:
            with open(self.health_log_path, 'r') as f:
                data = json.load(f)
        except (json.JSONDecodeError, FileNotFoundError):
            data = {"records": []}
        
        # Add new record
        new_record = {
            "timestamp": timestamp,
            "temperature": self.temperature,
            "activity_level": self.activity_level,
            "battery": self.battery,
            "health_score": self.health_score,
            "swarm_likelihood": self.swarm_likelihood,
            "brood_state": self.brood_state,
            "wasp_detected": self.wasp_detected,
            "wasp_verified": self.wasp_verified
        }
        
        data["records"].append(new_record)
        
        # Write back to file
        with open(self.health_log_path, 'w') as f:
            json.dump(data, f, indent=2)
        
        print(f"Saved health data to {self.health_log_path}")
        
    def upload_to_firebase(self):
        """Upload metrics to Firebase"""
        try:
            additional_data = {
                'brood_state': self.brood_state,
                'battery': self.battery
            }
            
            # Add wasp detection information
            if self.wasp_detected:
                additional_data['wasp_detected'] = True
                additional_data['wasp_verified'] = self.wasp_verified
            
            success = update_hive_data(
                user_id=USER_ID,
                hive_id=HIVE_ID,
                temperature=self.temperature,
                activity=self.activity_level,
                health=self.health_score,
                swarm_likelihood=self.swarm_likelihood,
                additional_data=additional_data
            )
            
            if success:
                print(f"Successfully updated Firebase with hive metrics")
                self.last_update_time = time.time()
                return True
            else:
                print("Failed to update Firebase")
                return False
        
        except Exception as e:
            print(f"Error uploading to Firebase: {e}")
            return False
            
    def check_alerts(self):
        """Check for alert conditions and send alerts if necessary"""
        # Temperature alerts
        check_threshold_and_alert(
            USER_ID, HIVE_ID, 
            'temperature', self.temperature, 
            threshold=35.0, comparison='greater', 
            severity='high'
        )
        
        check_threshold_and_alert(
            USER_ID, HIVE_ID, 
            'temperature', self.temperature, 
            threshold=32.0, comparison='less', 
            severity='medium'
        )
        
        # Activity alerts
        check_threshold_and_alert(
            USER_ID, HIVE_ID, 
            'activity', self.activity_level, 
            threshold=30.0, comparison='less', 
            severity='medium'
        )
        
        # Health score alerts
        check_threshold_and_alert(
            USER_ID, HIVE_ID, 
            'health', self.health_score, 
            threshold=50.0, comparison='less', 
            severity='high'
        )
        
        # Swarm likelihood alerts
        check_threshold_and_alert(
            USER_ID, HIVE_ID, 
            'swarm_likelihood', self.swarm_likelihood, 
            threshold=70, comparison='greater', 
            severity='high',
            title_format="Swarm Risk Alert",
            description_format="High risk of swarming detected! Current swarm likelihood: {value}%"
        )
        
        # Wasp detection alerts with verification
        if self.wasp_detected:
            # Run second camera verification if wasps detected
            verification_result = self.run_wasp_verification()
            
            if self.wasp_verified:
                # Both cameras detected wasps - high severity
                alert_id = send_alert(
                    USER_ID, HIVE_ID,
                    title="CRITICAL: Wasp/Hornet Invasion",
                    description="Wasps or hornets confirmed by multiple cameras! Immediate inspection recommended.",
                    severity="high"
                )
                print(f"⚠️⚠️ CRITICAL WASP ALERT SENT (Alert ID: {alert_id})")
            else:
                # Only primary camera detected wasps - medium severity
                alert_id = send_alert(
                    USER_ID, HIVE_ID,
                    title="Warning: Possible Wasp Detection",
                    description="Wasps or hornets may be present. Detection on primary camera only.",
                    severity="medium"
                )
                print(f"⚠️ Wasp warning alert sent (Alert ID: {alert_id})")
                
            # Reset wasp detection flags after sending alert
            self.wasp_detected = False
            self.wasp_verified = False
            
def firebase_flush_callback(monitor):
    """
    Immediately push all current data to Firebase when maintenance mode is activated
    
    Args:
        monitor: The HiveMonitor instance with the current monitoring data
    
    Returns:
        bool: True if push was successful, False otherwise
    """
    try:
        print("Maintenance mode activated - Pushing data to Firebase...")
        
        # Calculate latest metrics if needed
        monitor.read_latest_activity()
        monitor.calculate_health_score()
        monitor.calculate_swarm_likelihood()
        
        # Save locally first
        monitor.save_health_data()
        
        # Push to Firebase
        success = monitor.upload_to_firebase()
        
        if success:
            print("Data successfully pushed to Firebase - safe to proceed with maintenance")
            return True
        else:
            print("Failed to push data to Firebase!")
            return False
            
    except Exception as e:
        print(f"Error during Firebase push: {e}")
        return False

def init_spi():
    """Initialize SPI communication with error handling"""
    global spi
    try:
        spi = spidev.SpiDev()
        spi.open(0, 0)  # SPI bus 0, device 0
        spi.max_speed_hz = 50000
        spi.mode = 0b00  # SPI Mode 0
        spi.bits_per_word = 8
        print("SPI interface initialized successfully")
        return True
    except Exception as e:
        print(f"Error initializing SPI: {e}")
        print("Will use simulated data instead")
        spi = None
        return False
        
def main():
    """Main function to run the integrated bee monitoring system"""
    print("Starting Integrated Bee Monitoring System")
    
    # Initialize SPI
    spi_available = init_spi()
    
    # Initialize maintenance handler 
    maintenance = MaintenanceHandler(MAINTENANCE_SWITCH_PIN, LED_PIN)
    
    # Create instance of HiveMonitor
    monitor = HiveMonitor()
    
    # Register callback for maintenance mode
    maintenance.set_callback(lambda: firebase_flush_callback(monitor))
    
    # Add an outer loop to keep the program running continuously
    running = True
    while running:
        try:
            # Check if maintenance handler is properly initialized
            if not hasattr(maintenance, 'gpio_initialized') or not maintenance.gpio_initialized:
                print("\nWARNING: GPIO not properly initialized (pigpio daemon may not be running)")
                print("Enter commands:")
                print("  'on' = Turn maintenance mode ON (close switch)")
                print("  'off' = Turn maintenance mode OFF (open switch)")
                print("  'q' = Quit program")
                print(f"Continuing monitoring in {UPDATE_INTERVAL} seconds...")
                
                # Check for user input with timeout
                timeout = UPDATE_INTERVAL
                start_time = time.time()
                
                while time.time() - start_time < timeout:
                    # Check if there's input waiting (non-blocking)
                    ready_input = select.select([sys.stdin], [], [], 0.1)[0]
                    if ready_input:
                        key = sys.stdin.readline().strip().lower()
                        
                        if key == 'on':
                            try:
                                # Simulate maintenance switch closing
                                print("\n===== SIMULATING MAINTENANCE MODE ON =====")
                                maintenance._maintenance_on()
                                print("===== MAINTENANCE MODE SIMULATION COMPLETE =====\n")
                            except Exception as e:
                                print(f"Error during maintenance simulation: {e}")
                            
                            # After test, reset the wait timer
                            break
                                
                        elif key == 'off':
                            try:
                                # Simulate maintenance switch opening
                                print("\n===== SIMULATING MAINTENANCE MODE OFF =====")
                                maintenance._maintenance_off()
                                print("===== MAINTENANCE MODE SIMULATION COMPLETE =====\n")
                            except Exception as e:
                                print(f"Error during maintenance simulation: {e}")
                            
                            # After test, reset the wait timer
                            break
                                
                        elif key == 'q':
                            print("Quitting by user request")
                            running = False
                            return
                        
                        # Break the wait loop after processing input
                        break
                    
                    # Short sleep to prevent CPU hogging
                    time.sleep(0.1)
            
            # Regular data collection process
            # Get MCU data
            raw_data = monitor.read_mcu_data()
            # Process the data
            monitor.interpret_mcu_data(raw_data)
            # Calculate health and swarm likelihood
            monitor.calculate_health_score()
            monitor.calculate_swarm_likelihood()
            # Save data to log file
            monitor.save_health_data()
            
            # Upload to Firebase if it's time
            current_time = time.time()
            if current_time - monitor.last_update_time >= UPDATE_INTERVAL:
                monitor.upload_to_firebase()
                monitor.check_alerts()
            
            # Wait for next update
            print(f"Waiting {UPDATE_INTERVAL} seconds until next update...")
            time.sleep(UPDATE_INTERVAL)
            
        except KeyboardInterrupt:
            print("\nMonitoring system stopped by user")
            running = False
        except Exception as e:
            print(f"Error in monitoring system: {e}")
            import traceback
            traceback.print_exc()
            # Don't exit on error, try to continue
            time.sleep(5)  # Wait a bit before trying again
    
    # Only reach here when done running
    try:
        # Clean up maintenance handler resources
        maintenance.cleanup()
    except Exception as e:
        print(f"Warning during maintenance cleanup: {e}")
    
    # Clean up SPI resources
    if spi is not None:
        try:
            spi.close()
            print("SPI resources cleaned up")
        except:
            pass

if __name__ == "__main__":
    main()
