#use:   UserID:CO3Mde5N9qbJpS9TwJslvABpAOk2
#       hiveID: kjiqHt0pbaIMvAUFArp9
#       For Testing

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
            # Create the activity data point
            activity_data = {
                'day': firestore.SERVER_TIMESTAMP,
                'activity': activity
            }
            
            # Add activity to the main document
            data['activity'] = activity
            
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


###############################################################
###############################################################
###############################################################

import time
import random  # for demo purposes
from firebase_manager import update_hive_data, send_alert, check_threshold_and_alert

# Configuration
USER_ID = "CO3Mde5N9qbJpS9TwJslvABpAOk2"  # Your Firebase user ID
HIVE_IDS = ["T0XrPWUZ0SMeEX6LgKCX", "YourSecondHiveID"]  # Your hive IDs

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
