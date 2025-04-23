#!/usr/bin/env python3
import argparse
import time
import json
from pathlib import Path
import cv2
import torch
import torch.backends.cudnn as cudnn
from numpy import random
import numpy as np
import os
from datetime import datetime

from models.experimental import attempt_load
from utils.datasets import LoadImages
from utils.general import check_img_size, non_max_suppression, \
                scale_coords, set_logging
from utils.torch_utils import select_device, time_synchronized, TracedModel

from sort import *
from upload_utils import update_hive_data, send_alert

# Configuration for Firebase
USER_ID = "CO3Mde5N9qbJpS9TwJslvABpAOk2"  # Your Firebase user ID
HIVE_ID = "T0XrPWUZ0SMeEX6LgKCX"  # Your hive ID
RECORDING_LENGTH = 60  # Recording length in seconds

# Bee tracking statistics
class BeeStats:
    def __init__(self, json_path="activity_log.json"):
        self.total_bees_detected = 0
        self.unique_bees = set()
        self.entering_hive = 0
        self.leaving_hive = 0
        self.potential_wasps = 0
        self.wasp_alerts = 0
        self.last_report_time = time.time()
        self.json_path = json_path
        self.activity_level = 0
        
        # Initialize JSON file if it doesn't exist
        if not os.path.exists(self.json_path):
            directory = os.path.dirname(self.json_path)
            if directory and not os.path.exists(directory):
                os.makedirs(directory)
            with open(self.json_path, 'w') as f:
                json.dump({"activity_records": []}, f)
        
        # Movement tracking dict: {id: {"positions": [], "direction": None, "counted": False, "is_wasp": False}}
        self.tracks = {}
        
    def calculate_activity(self, frame_height, frame_width):
        """Calculate activity level based on bee movements and counts"""
        
        # Base activity on the number of unique bees and their movement patterns
        bee_count_factor = min(len(self.unique_bees) * 5, 50)  # Max 50 points from bee count
        
        # Calculate movement factor based on entering/leaving activity
        movement_ratio = 0
        total_directional = self.entering_hive + self.leaving_hive
        if len(self.unique_bees) > 0:
            movement_ratio = total_directional / len(self.unique_bees)
        
        movement_factor = min(movement_ratio * 50, 50)  # Max 50 points from movement
        
        # Calculate overall activity (0-100)
        self.activity_level = int(bee_count_factor + movement_factor)
        
        return self.activity_level
    
    def save_to_json(self):
        """Save current activity level to JSON file"""
        timestamp = datetime.now().isoformat()
        
        # Read existing data
        try:
            with open(self.json_path, 'r') as f:
                data = json.load(f)
        except (json.JSONDecodeError, FileNotFoundError):
            data = {"activity_records": []}
        
        # Add new record
        new_record = {
            "timestamp": timestamp,
            "activity_level": self.activity_level,
            "bee_count": len(self.unique_bees),
            "entering": self.entering_hive,
            "leaving": self.leaving_hive,
            "wasps_detected": self.potential_wasps
        }
        
        data["activity_records"].append(new_record)
        
        # Write back to file
        with open(self.json_path, 'w') as f:
            json.dump(data, f, indent=2)
        
        print(f"Saved activity level {self.activity_level} to {self.json_path}")
        
    def update_stats(self, frame_height):
        # Process all tracks to determine bee movements and potential wasps
        for track_id, data in list(self.tracks.items()):
            positions = data["positions"]
            
            # Skip if not enough positions to determine direction
            if len(positions) < 5:
                continue
                
            # Check for potential wasps (hovering in top third)
            top_third = frame_height / 3
            if not data["is_wasp"]:
                # Check if the bee is mostly in the top third
                top_third_positions = [p for p in positions if p[1] < top_third]
                if len(top_third_positions) > len(positions) * 0.7:
                    # Check if it's hovering (not moving much horizontally)
                    x_positions = [p[0] for p in positions[-10:]] if len(positions) >= 10 else [p[0] for p in positions]
                    x_movement = max(x_positions) - min(x_positions)
                    if x_movement < 50:  # Threshold for horizontal movement
                        data["is_wasp"] = True
                        self.potential_wasps += 1
                        self.wasp_alerts += 1
                        print(f"⚠️ ALERT: Potential wasp detected (ID: {track_id})")
            
            # Determine direction if not already counted
            if not data["counted"] and len(positions) >= 10:
                # Use the first and last 5 positions to determine direction
                start_x = sum(p[0] for p in positions[:5]) / 5
                end_x = sum(p[0] for p in positions[-5:]) / 5
                
                # Calculate horizontal movement
                x_diff = end_x - start_x
                
                # Set threshold for significant movement
                if abs(x_diff) > 40:  # Minimum pixel movement to count as directional
                    if x_diff > 0:  # Moving right (entering hive)
                        data["direction"] = "entering"
                        self.entering_hive += 1
                    else:  # Moving left (leaving hive)
                        data["direction"] = "leaving"
                        self.leaving_hive += 1
                    
                    data["counted"] = True
    
    def add_position(self, track_id, x, y):
        if track_id not in self.unique_bees:
            self.unique_bees.add(track_id)
            self.total_bees_detected += 1
            
        if track_id not in self.tracks:
            self.tracks[track_id] = {
                "positions": [],
                "direction": None,
                "counted": False,
                "is_wasp": False
            }
            
        self.tracks[track_id]["positions"].append((x, y))
        
        # Keep only the last 30 positions to avoid memory issues
        if len(self.tracks[track_id]["positions"]) > 30:
            self.tracks[track_id]["positions"].pop(0)

# Function to draw bounding boxes (simplified for our needs)
def draw_boxes(img, bbox, identities=None, categories=None, names=None, colors=None, bee_stats=None):
    frame_height = img.shape[0]
    
    for i, box in enumerate(bbox):
        x1, y1, x2, y2 = [int(i) for i in box]
        tl = opt.thickness or round(0.002 * (img.shape[0] + img.shape[1]) / 2) + 1
        
        # Calculate center point
        center_x = (x1 + x2) // 2
        center_y = (y1 + y2) // 2

        cat = int(categories[i]) if categories is not None else 0
        id = int(identities[i]) if identities is not None else 0
        
        # Update bee positions and stats
        if identities is not None and bee_stats is not None:
            bee_stats.add_position(id, center_x, center_y)
        
        # Determine color based on status
        color = colors[cat]
        
        # For tracked objects, check if it's a potential wasp
        if identities is not None and bee_stats is not None and id in bee_stats.tracks:
            track_data = bee_stats.tracks[id]
            
            # Mark potential wasps in red
            if track_data["is_wasp"]:
                color = (0, 0, 255)  # Red for wasps
            # Mark entering bees in green
            elif track_data["direction"] == "entering":
                color = (0, 255, 0)  # Green for entering
            # Mark leaving bees in blue
            elif track_data["direction"] == "leaving":
                color = (255, 0, 0)  # Blue for leaving
        
        # Draw rectangle and label as before
        cv2.rectangle(img, (x1, y1), (x2, y2), color, tl)
        
        if identities is not None:
            label = f"{id}:{names[cat]}" 
            if bee_stats is not None and id in bee_stats.tracks:
                track_data = bee_stats.tracks[id]
                if track_data["is_wasp"]:
                    label += " [WASP!]"
                elif track_data["direction"] == "entering":
                    label += " [IN]"
                elif track_data["direction"] == "leaving":
                    label += " [OUT]"
        else:
            label = f'{names[cat]}'
        
        tf = max(tl - 1, 1)
        t_size = cv2.getTextSize(label, 0, fontScale=tl / 3, thickness=tf)[0]
        c2 = x1 + t_size[0], y1 - t_size[1] - 3
        cv2.rectangle(img, (x1, y1), c2, color, -1, cv2.LINE_AA)
        cv2.putText(img, label, (x1, y1 - 2), 0, tl / 3, [225, 255, 255], thickness=tf, lineType=cv2.LINE_AA)

    # Add overall stats to the frame
    if bee_stats is not None:
        stats_text = [
            f"Total Bees: {len(bee_stats.unique_bees)}",
            f"Entering Hive: {bee_stats.entering_hive}",
            f"Leaving Hive: {bee_stats.leaving_hive}",
            f"Activity Level: {bee_stats.activity_level}%"
        ]
        
        # Display wasp alert if detected
        if bee_stats.wasp_alerts > 0:
            cv2.putText(img, "⚠️ WASP ALERT ⚠️", (img.shape[1]//2 - 150, 50), 
                      cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255), 3, cv2.LINE_AA)
        
        for i, text in enumerate(stats_text):
            y_pos = 30 + i * 30
            cv2.putText(img, text, (10, y_pos), cv2.FONT_HERSHEY_SIMPLEX, 
                      0.7, (255, 255, 255), 2, cv2.LINE_AA)

    return img

def record_video(camera_source, output_path, duration=60, fps=30):
    """
    Record video from the camera for a specified duration
    
    Args:
        camera_source: Camera index or URL
        output_path: Where to save the video
        duration: Recording duration in seconds
        fps: Frames per second
    
    Returns:
        True if recording was successful, False otherwise
    """
    try:
        # Initialize camera
        print(f"Initializing camera {camera_source}...")
        if camera_source.isdigit():
            cap = cv2.VideoCapture(int(camera_source))
        else:
            cap = cv2.VideoCapture(camera_source)
        
        if not cap.isOpened():
            print(f"Error: Could not open camera {camera_source}")
            return False
        
        # Get camera properties
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        
        # Create VideoWriter object
        fourcc = cv2.VideoWriter_fourcc(*'XVID')
        out = cv2.VideoWriter(output_path, fourcc, fps, (width, height))
        
        print(f"Starting {duration} second video recording...")
        start_time = time.time()
        frames_recorded = 0
        
        # Record for specified duration
        while time.time() - start_time < duration:
            ret, frame = cap.read()
            if not ret:
                print("Error reading frame from camera")
                break
                
            # Write frame to video file
            out.write(frame)
            frames_recorded += 1
            
            # Optional: display recording status
            elapsed = time.time() - start_time
            remaining = max(0, duration - elapsed)
            if frames_recorded % 30 == 0:  # Update status every 30 frames
                print(f"Recording: {elapsed:.1f}s elapsed, {remaining:.1f}s remaining")
            
            # Wait a small amount to control frame rate
            time.sleep(1/fps)
        
        # Release resources
        out.release()
        cap.release()
        
        print(f"Recording complete: {frames_recorded} frames captured in {time.time() - start_time:.1f} seconds")
        print(f"Video saved to {output_path}")
        
        return True
    
    except Exception as e:
        print(f"Error during video recording: {e}")
        return False

def analyze_video(video_path, bee_stats):
    """
    Analyze a recorded video file for bee activity
    
    Args:
        video_path: Path to the video file
        bee_stats: BeeStats object to update
    
    Returns:
        Updated bee_stats object
    """
    source = video_path
    weights = opt.weights
    imgsz = opt.img_size
    save_dir = Path(opt.project) / opt.name
    save_dir.mkdir(parents=True, exist_ok=True)
    
    # Initialize
    set_logging()
    device = select_device(opt.device)
    half = device.type != 'cpu'  # half precision only supported on CUDA

    # Load model
    print("Loading YOLOv7 model...")
    model = attempt_load(weights, map_location=device)
    stride = int(model.stride.max())
    imgsz = check_img_size(imgsz, s=stride)
    model = TracedModel(model, device, opt.img_size)
    
    if half:
        model.half()

    # Set Dataloader
    dataset = LoadImages(source, img_size=imgsz, stride=stride)

    # Get names and colors
    names = model.module.names if hasattr(model, 'module') else model.names
    colors = [[random.randint(0, 255) for _ in range(3)] for _ in names]

    # Initialize bee tracking
    sort_tracker = Sort(max_age=5, min_hits=2, iou_threshold=0.3)
    
    # Run inference
    if device.type != 'cpu':
        model(torch.zeros(1, 3, imgsz, imgsz).to(device).type_as(next(model.parameters())))
    
    frame_count = 0
    total_frames = 0
    
    # Count total frames for progress indicator
    temp_cap = cv2.VideoCapture(source)
    if temp_cap.isOpened():
        total_frames = int(temp_cap.get(cv2.CAP_PROP_FRAME_COUNT))
    temp_cap.release()
    
    print(f"Starting analysis of {source} ({total_frames} frames)...")
    t0 = time.time()
    
    # Reset bee stats for this analysis session
    bee_stats.unique_bees.clear()
    bee_stats.tracks.clear()
    bee_stats.entering_hive = 0
    bee_stats.leaving_hive = 0
    bee_stats.potential_wasps = 0
    bee_stats.wasp_alerts = 0
    bee_stats.total_bees_detected = 0
    
    # Create output directory for visualization if needed
    if not opt.nosave:
        vis_dir = save_dir / 'visualization'
        vis_dir.mkdir(exist_ok=True)
        output_video_path = str(vis_dir / f'analyzed_{Path(source).name}')
        output_video = None
    
    for path, img, im0s, vid_cap in dataset:
        frame_count += 1
        
        # Print progress
        if frame_count % 10 == 0 or frame_count == 1:
            progress = (frame_count / total_frames * 100) if total_frames > 0 else 0
            print(f"Analyzing frame {frame_count}/{total_frames} ({progress:.1f}%)")
        
        img = torch.from_numpy(img).to(device)
        img = img.half() if half else img.float()
        img /= 255.0
        if img.ndimension() == 3:
            img = img.unsqueeze(0)

        # Inference
        pred = model(img, augment=opt.augment)[0]

        # Apply NMS
        pred = non_max_suppression(pred, opt.conf_thres, opt.iou_thres, classes=opt.classes, agnostic=opt.agnostic_nms)

        # Process detections
        for i, det in enumerate(pred):
            p, s, im0, frame = path, '', im0s, getattr(dataset, 'frame', 0)
            
            if len(det):
                # Rescale boxes from img_size to im0 size
                det[:, :4] = scale_coords(img.shape[2:], det[:, :4], im0.shape).round()

                # Format detections for tracking
                dets_to_sort = np.empty((0, 6))
                for x1, y1, x2, y2, conf, detclass in det.cpu().detach().numpy():
                    dets_to_sort = np.vstack((dets_to_sort, 
                                             np.array([x1, y1, x2, y2, conf, detclass])))
                
                # Track objects
                tracked_dets = sort_tracker.update(dets_to_sort, True)
                tracks = sort_tracker.getTrackers()
                
                # Update bee statistics
                bee_stats.update_stats(im0.shape[0])
                
                # Draw boxes for visualization
                if len(tracked_dets) > 0:
                    bbox_xyxy = tracked_dets[:, :4]
                    identities = tracked_dets[:, 8]
                    categories = tracked_dets[:, 4]
                    
                    im0 = draw_boxes(im0, bbox_xyxy, identities, categories, names, colors, bee_stats)
            
            # Save visualized frame if requested
            if not opt.nosave:
                if output_video is None:
                    # Initialize video writer
                    fps = vid_cap.get(cv2.CAP_PROP_FPS)
                    w = int(vid_cap.get(cv2.CAP_PROP_FRAME_WIDTH))
                    h = int(vid_cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
                    output_video = cv2.VideoWriter(output_video_path, 
                                                  cv2.VideoWriter_fourcc(*'XVID'), 
                                                  fps, (w, h))
                output_video.write(im0)
    
    # Calculate final activity level
    bee_stats.calculate_activity(im0.shape[0], im0.shape[1])
    
    # Release resources
    if not opt.nosave and output_video is not None:
        output_video.release()
    
    analysis_time = time.time() - t0
    
    # Print analysis results
    print("\n===== Video Analysis Results =====")
    print(f"Total unique bees detected: {len(bee_stats.unique_bees)}")
    print(f"Bees entering hive: {bee_stats.entering_hive}")
    print(f"Bees leaving hive: {bee_stats.leaving_hive}")
    print(f"Potential wasps detected: {bee_stats.potential_wasps}")
    print(f"Activity level: {bee_stats.activity_level}%")
    print(f"Analysis completed in {analysis_time:.1f} seconds")
    print("==================================\n")
    
    return bee_stats

def detect_and_monitor():
    """
    Main function to run the continuous bee monitoring system:
    1. Record video for a set duration
    2. Analyze the recorded video
    3. Update Firebase and save data
    4. Repeat
    """
    save_dir = Path(opt.project) / opt.name
    save_dir.mkdir(parents=True, exist_ok=True)
    
    # Directory for temporary video recordings
    recordings_dir = save_dir / 'recordings'
    recordings_dir.mkdir(exist_ok=True)
    
    # Initialize bee statistics
    bee_stats = BeeStats(json_path=opt.json_path)
    
    cycle = 1
    running = True
    
    print(f"Starting bee monitoring system. Press Ctrl+C to exit.")
    
    try:
        while running:
            # Create timestamped filename for this recording
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            video_path = str(recordings_dir / f"recording_{timestamp}.avi")
            
            print(f"\n===== Starting monitoring cycle {cycle} =====")
            
            # Step 1: Record video
            print(f"Step 1: Recording video for {RECORDING_LENGTH} seconds...")
            recording_success = record_video(
                opt.source, 
                video_path, 
                duration=RECORDING_LENGTH,
                fps=opt.fps
            )
            
            if not recording_success:
                print("Recording failed, trying again in 10 seconds...")
                time.sleep(10)
                continue
            
            # Step 2: Analyze video
            print(f"Step 2: Analyzing video for bee activity...")
            bee_stats = analyze_video(video_path, bee_stats)
            
            # Step 3: Update Firebase with activity data
            print(f"Step 3: Updating Firebase and saving data...")
            update_success = update_hive_data(
                USER_ID, 
                HIVE_ID, 
                activity=bee_stats.activity_level
            )
            
            if update_success:
                print(f"Updated Firebase with activity level: {bee_stats.activity_level}")
            else:
                print("Failed to update Firebase")
            
            # Step 4: Save activity data to JSON
            bee_stats.save_to_json()
            
            # Step 5: Send wasp alert if detected
            if bee_stats.potential_wasps > 0:
                alert_id = send_alert(
                    USER_ID,
                    HIVE_ID,
                    title="Wasp Detection Alert",
                    description=f"Detected {bee_stats.potential_wasps} potential wasps near your hive.",
                    severity="medium"  # Will be escalated to high if verified by second camera
                )
                
                if alert_id:
                    print(f"Sent wasp alert to Firebase (Alert ID: {alert_id})")
                else:
                    print("Failed to send wasp alert")
            
            # Cleanup old recordings if needed (keep only the last 5)
            old_recordings = sorted(list(recordings_dir.glob("*.avi")))[:-5]
            for old_file in old_recordings:
                try:
                    old_file.unlink()
                    print(f"Removed old recording: {old_file}")
                except Exception as e:
                    print(f"Error removing old recording {old_file}: {e}")
            
            # Optional: Wait a short time before next cycle
            print(f"Cycle {cycle} complete. Starting next cycle in 5 seconds...")
            time.sleep(5)
            
            cycle += 1
            
    except KeyboardInterrupt:
        print("Keyboard interrupt detected. Shutting down...")
    finally:
        print("\n===== Bee Monitoring System Summary =====")
        print(f"Completed {cycle-1} monitoring cycles")
        print(f"Last activity level: {bee_stats.activity_level}%")
        print("=========================================\n")
        
        # Final cleanup
        cv2.destroyAllWindows()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--weights', type=str, default='yolov7.pt', help='model.pt path')
    parser.add_argument('--source', type=str, default='0', help='source (use 16 for IP camera)')
    parser.add_argument('--img-size', type=int, default=640, help='inference size (pixels)')
    parser.add_argument('--conf-thres', type=float, default=0.25, help='object confidence threshold')
    parser.add_argument('--iou-thres', type=float, default=0.45, help='IOU threshold for NMS')
    parser.add_argument('--device', default='', help='cuda device, i.e. 0 or 0,1,2,3 or cpu')
    parser.add_argument('--nosave', action='store_true', help='do not save images/videos')
    parser.add_argument('--view-img', action='store_true', help='display results')
    parser.add_argument('--classes', nargs='+', type=int, help='filter by class: --class 0, or --class 0 2 3')
    parser.add_argument('--agnostic-nms', action='store_true', help='class-agnostic NMS')
    parser.add_argument('--augment', action='store_true', help='augmented inference')
    parser.add_argument('--project', default='runs/detect', help='save results to project/name')
    parser.add_argument('--name', default='bee_monitor', help='save results to project/name')
    parser.add_argument('--thickness', type=int, default=2, help='bounding box thickness')
    parser.add_argument('--json-path', type=str, default='activity_log.json', help='path to save JSON activity log')
    parser.add_argument('--record-length', type=int, default=60, help='length of each video recording in seconds')
    parser.add_argument('--fps', type=int, default=10, help='frames per second for recording (lower is better for Raspberry Pi)')
    
    opt = parser.parse_args()
    print(opt)
    
    # Update global recording length if specified via command line
    RECORDING_LENGTH = opt.record_length
    
    # Check if the source is valid
    if opt.source.isdigit() or opt.source.startswith(('rtsp://', 'rtmp://', 'http://')):
        print(f"Using camera source: {opt.source}")
    else:
        print(f"Warning: Source '{opt.source}' may not be a valid camera. Check your camera configuration.")
    
    try:
        # Run the monitoring function
        detect_and_monitor()
    except KeyboardInterrupt:
        print("Program terminated by user")
    except Exception as e:
        print(f"Error in main program: {e}")
        import traceback
        traceback.print_exc()
