#use:   UserID:CO3Mde5N9qbJpS9TwJslvABpAOk2
#       hiveID: kjiqHt0pbaIMvAUFArp9
#       For Testing
import datetime
import pytz
import time
import lgpio
import threading

# Get the current time in BST (British Summer Time)
uk_timezone = pytz.timezone('Europe/London')  # This will automatically handle BST/GMT switching
current_time = datetime.datetime.now(uk_timezone)

def update_hive_data(user_id, hive_id, temperature=None, activity=None, health=None, 
                    swarm_likelihood=None, additional_data=None):
    """
    Update hive data in Firestore with specific parameters.
    
    Parameters:
    - user_id: The user ID who owns the hive
    - hive_id: The ID of the hive to update
    - temperature: Current temperature in the hive (°C)
    - activity: Current activity level (0-100)
    - health: Current health percentage (0-100)
    - swarm_likelihood: Current swarm likelihood percentage (0-100)
    - additional_data: Optional dictionary with any additional data to update
    
    Returns:
    - True if successful, False otherwise
    """
    try:
        # Create data dictionary with provided parameters
        data = {}
        
        # Add provided parameters to data dictionary
        if temperature is not None:
            data['temperature'] = temperature
        
        # Add activity to the main document if provided
        if activity is not None:
            data['activity'] = activity
            
        if health is not None:
            data['health'] = health
            
        if swarm_likelihood is not None:
            data['swarm_likelihood'] = swarm_likelihood
            
        # Add any additional data
        if additional_data and isinstance(additional_data, dict):
            data.update(additional_data)
            
        # Add timestamp
        data['last_updated'] = firestore.SERVER_TIMESTAMP
        
        # If there's no data to update, return early
        if len(data) <= 1:  # Only has timestamp
            print("No data provided to update")
            return False
            
        # Update the hive document
        hive_ref = db.collection('users').document(user_id).collection('beehives').document(hive_id)
        hive_ref.update(data)
        
        # If activity data is provided, add it to the activity_over_time array
        if activity is not None:
            # Use a client-side timestamp for the array
            import datetime
            current_time = datetime.datetime.now(uk_timezone)
            
            # Create the activity data point with client-side timestamp
            activity_data = {
                'day': current_time,
                'activity': activity
            }
                
            # Add to the activity_over_time array using array_union
            hive_ref.update({
                'activity_over_time': firestore.ArrayUnion([activity_data])
            })
        
        print(f"Updated hive {hive_id} for user {user_id} with data: {data}")
        return True
    
    except Exception as e:
        print(f"Error updating hive data: {e}")
        return False

def send_alert(user_id, hive_id, title, description, severity='medium'):
    """
    Send an alert to Firestore.
    
    Parameters:
    - user_id: The user ID who owns the hive
    - hive_id: The ID of the hive the alert is for
    - title: Alert title
    - description: Alert description
    - severity: Alert severity ('low', 'medium', or 'high')
    
    Returns:
    - The ID of the created alert if successful, None otherwise
    """
    try:
        # Get the hive name
        hive_ref = db.collection('users').document(user_id).collection('beehives').document(hive_id)
        hive_doc = hive_ref.get()
        
        if not hive_doc.exists:
            print(f"Hive {hive_id} not found for user {user_id}")
            return None
        
        hive_data = hive_doc.to_dict()
        hive_name = hive_data.get('name', 'Unknown Hive')
        
        # Create alert data
        alert_data = {
            'title': title,
            'description': description,
            'date': firestore.SERVER_TIMESTAMP,
            'hiveName': hive_name,
            'hiveId': hive_id,
            'read': False,
            'severity': severity
        }
        
        # Add to alerts collection
        alert_ref = db.collection('users').document(user_id).collection('alerts').document()
        alert_ref.set(alert_data)
        
        print(f"Created alert '{title}' for hive {hive_id} (user {user_id})")
        return alert_ref.id
    
    except Exception as e:
        print(f"Error sending alert: {e}")
        return None

def check_threshold_and_alert(user_id, hive_id, measurement_type, value, threshold, comparison='greater', 
                              title_format=None, description_format=None, severity='medium'):
    """
    Check if a measurement exceeds a threshold and send an alert if it does.
    
    Parameters:
    - user_id: The user ID who owns the hive
    - hive_id: The ID of the hive to check
    - measurement_type: Type of measurement (e.g., 'temperature', 'activity')
    - value: Current value of the measurement
    - threshold: Threshold value to compare against
    - comparison: 'greater' or 'less' - whether to alert when value is greater or less than threshold
    - title_format: Format string for alert title (defaults based on measurement_type)
    - description_format: Format string for alert description (defaults based on measurement_type)
    - severity: Alert severity ('low', 'medium', or 'high')
    
    Returns:
    - The ID of the created alert if threshold was exceeded, None otherwise
    """
    exceeded = False
    
    if comparison == 'greater':
        exceeded = value > threshold
    elif comparison == 'less':
        exceeded = value < threshold
    else:
        print(f"Invalid comparison type: {comparison}")
        return None
    
    if not exceeded:
        return None
    
    # Default title and description formats based on measurement type
    if title_format is None:
        if measurement_type == 'temperature':
            title_format = "High Temperature Alert"
        elif measurement_type == 'activity':
            title_format = "Activity Level Alert"
        else:
            title_format = f"{measurement_type.title()} Alert"
    
    if description_format is None:
        if comparison == 'greater':
            description_format = f"The {measurement_type} has exceeded the safe limit of {threshold}. Current value: {value}."
        else:
            description_format = f"The {measurement_type} has fallen below the safe limit of {threshold}. Current value: {value}."
    else:
        description_format = description_format.format(value=value, threshold=threshold)
    
    return send_alert(user_id, hive_id, title_format, description_format, severity)



class MaintenanceHandler:
    """
    Handles maintenance mode interrupts using lgpio, which is compatible
    with Raspberry Pi 5.
    """
    
    def __init__(self, maintenance_switch_pin, led_pin):
        """
        Initialize the maintenance handler.
        
        Args:
            maintenance_switch_pin (int): GPIO pin connected to the maintenance switch
            led_pin (int): GPIO pin connected to the green LED
        """
        self.maintenance_switch_pin = maintenance_switch_pin
        self.led_pin = led_pin
        self.callback_fn = None
        self.in_maintenance_mode = False
        self.gpio_initialized = False
        self.monitoring = False
        
        try:
            # Try to find the correct chip number
            # For Raspberry Pi 5, it's typically 4
            chip_numbers = [4, 0, 1, 2, 3]
            self.h = None
            
            for chip_num in chip_numbers:
                try:
                    self.h = lgpio.gpiochip_open(chip_num)
                    print(f"Successfully opened GPIO chip {chip_num}")
                    break
                except Exception as e:
                    print(f"Failed to open GPIO chip {chip_num}: {e}")
            
            if self.h is None:
                print("Could not open any GPIO chip. GPIO functionality will be disabled.")
                return
            
            # Set up LED pin as output
            lgpio.gpio_claim_output(self.h, led_pin)
            lgpio.gpio_write(self.h, led_pin, 0)  # Start with LED off
            print(f"LED pin {led_pin} configured successfully")
            
            # Set up switch pin as input with pull-down
            lgpio.gpio_claim_input(self.h, maintenance_switch_pin, lgpio.SET_PULL_DOWN)
            print(f"Switch pin {maintenance_switch_pin} configured successfully")
            
            # Start a monitoring thread for the switch
            self.gpio_initialized = True
            self.monitoring = True
            self.monitor_thread = threading.Thread(target=self._monitor_switch, daemon=True)
            self.monitor_thread.start()
            print("GPIO monitoring thread started")
            
        except Exception as e:
            print(f"Error initializing GPIO with lgpio: {e}")
            print("GPIO functionality will be disabled")
    
    def _monitor_switch(self):
        """Continuously monitor the switch pin"""
        last_state = 0
        while self.monitoring:
            try:
                # Read current state
                current_state = lgpio.gpio_read(self.h, self.maintenance_switch_pin)
                
                # Check for state change
                if current_state != last_state:
                    if current_state == 1:  # Button pressed
                        self._maintenance_on()
                    else:  # Button released
                        self._maintenance_off()
                    
                    last_state = current_state
                
                # Short sleep to prevent CPU hogging
                time.sleep(0.1)
                
            except Exception as e:
                print(f"Error in GPIO monitoring: {e}")
                time.sleep(1)  # Longer sleep on error
    
    def set_callback(self, callback_fn):
        """Set a callback function to be called when maintenance mode is activated"""
        self.callback_fn = callback_fn
    
    def _maintenance_on(self):
        """Handler for rising edge (switch closed) - entering maintenance mode."""
        if self.in_maintenance_mode or not self.gpio_initialized:
            return
            
        self.in_maintenance_mode = True
        print("Maintenance switch activated - preparing for maintenance mode")
        
        # Execute any cleanup operations if specified
        if self.callback_fn:
            try:
                self.callback_fn()
            except Exception as e:
                print(f"Error in maintenance callback: {e}")
        
        # Indicate it's safe to proceed with maintenance by turning on the LED
        try:
            lgpio.gpio_write(self.h, self.led_pin, 1)  # LED on
            print("Maintenance mode active - LED turned ON")
        except Exception as e:
            print(f"Error setting LED: {e}")
    
    def _maintenance_off(self):
        """Handler for falling edge (switch opened) - exiting maintenance mode."""
        if not self.in_maintenance_mode or not self.gpio_initialized:
            return
            
        self.in_maintenance_mode = False
        
        # Turn off the LED
        try:
            lgpio.gpio_write(self.h, self.led_pin, 0)  # LED off
            print("Maintenance mode deactivated - LED turned OFF")
        except Exception as e:
            print(f"Error setting LED: {e}")
    
    def cleanup(self):
        """Clean up GPIO resources when done."""
        if not self.gpio_initialized:
            return
            
        try:
            # Stop the monitoring thread
            self.monitoring = False
            if hasattr(self, 'monitor_thread') and self.monitor_thread.is_alive():
                self.monitor_thread.join(timeout=1.0)
            
            # Release GPIO resources
            lgpio.gpio_free(self.h, self.led_pin)
            lgpio.gpio_free(self.h, self.maintenance_switch_pin)
            lgpio.gpiochip_close(self.h)
            
            print("GPIO resources cleaned up")
        except Exception as e:
            print(f"Error during GPIO cleanup: {e}")


###############################################################
###############################################################
###############################################################

import firebase_admin
from firebase_admin import credentials, firestore
import time
import random  # for demo purposes

# Initialize Firebase if not already initialized
if not firebase_admin._apps:
    try:
        cred = credentials.Certificate("serviceAccountKey.json")  # Ensure this file is present
        firebase_admin.initialize_app(cred)
        print("Firebase initialized successfully")
    except Exception as e:
        print(f"Error initializing Firebase: {e}")
        print("Continuing in demo mode without Firebase")

# Get Firestore database instance
try:
    db = firestore.client()
except Exception as e:
    print(f"Error connecting to Firestore: {e}")
    # Create a mock db object for testing
    class MockDB:
        def collection(self, *args):
            return self
        def document(self, *args):
            return self
        def update(self, *args):
            print(f"MOCK: Would update with {args}")
            return True
        def set(self, *args):
            print(f"MOCK: Would set with {args}")
            return True
        def get(self, *args):
            class MockDoc:
                def exists(self):
                    return True
                def to_dict(self):
                    return {"name": "Test Hive"}
            return MockDoc()
    
    db = MockDB()
    print("Using mock database for testing")

# Configuration
USER_ID = "CO3Mde5N9qbJpS9TwJslvABpAOk2"  # Your Firebase user ID
HIVE_IDS = ["kjiqHt0pbaIMvAUFArp9", "YourSecondHiveID"]  # Your hive IDs

# Thresholds for alerts
TEMP_HIGH_THRESHOLD = 35.0  # °C
TEMP_LOW_THRESHOLD = 32.0   # °C
ACTIVITY_LOW_THRESHOLD = 40  # Activity percentage

# Function to read sensor data (replace with your actual sensor code)
def read_sensor_data(hive_id):
    """Simulate or read actual sensor data from hardware"""
    # In a real implementation, you would get data from your sensors
    # This is just a simulation
    return {
        'temperature': 34.5 + random.uniform(-3, 3),  # Random temperature around 34.5°C
        'activity': 75 + random.uniform(-20, 15),     # Random activity level
        'health': max(50, min(95, 75 + random.uniform(-10, 10))),  # Health between 50-95%
        'swarm_likelihood': max(0, min(80, 20 + random.uniform(-10, 30)))  # Swarm likelihood 0-80%
    }

# Add a test/demo function
def test_maintenance_handler():
    """Test the MaintenanceHandler with physical GPIO pins"""
    SWITCH_PIN = 17  # GPIO pin for maintenance switch
    LED_PIN = 27     # GPIO pin for LED indicator
    
    print("Testing MaintenanceHandler with pigpio...")
    print("Connect a button to GPIO 17 and an LED to GPIO 27")
    print("Press Ctrl+C to exit")
    
    # Create a handler instance
    handler = MaintenanceHandler(SWITCH_PIN, LED_PIN)
    
    # Set a test callback
    handler.set_callback(lambda: print("Maintenance mode callback executed!"))
    
    try:
        # Keep running until interrupted
        while True:
            print("Waiting for button press... (Ctrl+C to exit)")
            time.sleep(5)
    except KeyboardInterrupt:
        print("Test interrupted")
    finally:
        handler.cleanup()
        print("Test complete")

# For running this file directly as a test
if __name__ == "__main__":
    print("Running upload_utils.py in test mode")
    test_maintenance_handler()
