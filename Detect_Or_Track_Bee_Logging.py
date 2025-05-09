import argparse
import time
from pathlib import Path
import cv2
import torch
import torch.backends.cudnn as cudnn
from numpy import random
import numpy as np
import firebase_admin
from firebase_admin import credentials, db


from models.experimental import attempt_load
from utils.datasets import LoadStreams, LoadImages
from utils.general import check_img_size, check_requirements, \
                check_imshow, non_max_suppression, apply_classifier, \
                scale_coords, xyxy2xywh, strip_optimizer, set_logging, \
                increment_path
from utils.plots import plot_one_box
from utils.torch_utils import select_device, load_classifier, time_synchronized, TracedModel

from sort import *

# Initialize Firebase
cred = credentials.Certificate("beehive-monitor-firebase-key.json")  # Adjust path if needed
firebase_admin.initialize_app(cred, {
    'databaseURL': 'https://beehive-monitor-4e330-default-rtdb.europe-west1.firebasedatabase.app/'
})

ref = db.reference('bee-data-test1')

# Bee tracking statistics
class BeeStats:
    def __init__(self):
        self.total_bees_detected = 0
        self.unique_bees = set()
        self.entering_hive = 0
        self.leaving_hive = 0
        self.potential_wasps = 0
        self.wasp_alerts = 0
        self.last_firebase_push = time.time()
        self.firebase_push_interval = 60  # Push every 60 seconds
        
        # Movement tracking dict: {id: {"positions": [], "direction": None, "counted": False, "is_wasp": False}}
        self.tracks = {}
    
    def push_to_firebase(self):
        snapshot = {
            'total_bees_detected': len(self.unique_bees),
            'entering_hive': self.entering_hive,
            'leaving_hive': self.leaving_hive,
            'potential_wasps': self.potential_wasps,
            'timestamp': time.strftime("%Y-%m-%d %H:%M:%S")
        }
        try:
            ref.push(snapshot)
            print("Stats pushed to Firebase.")
        except Exception as e:
            print(f"Firebase push failed: {e}")
        
    def check_and_push_firebase(self):
        """Check if it's time to push data to Firebase and do so if needed"""
        if time.time() - self.last_firebase_push >= self.firebase_push_interval:
            self.push_to_firebase()
            self.last_firebase_push = time.time()
            return True
        return False
        
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
                        # Only print alert in non-headless mode or if verbose
                        if not getattr(opt, 'headless', False) or getattr(opt, 'verbose', False):
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


"""Function to Draw Bounding boxes"""
def draw_boxes(img, bbox, identities=None, categories=None, confidences=None, names=None, colors=None, bee_stats=None):
    frame_height = img.shape[0]
    
    for i, box in enumerate(bbox):
        x1, y1, x2, y2 = [int(i) for i in box]
        tl = opt.thickness or round(0.002 * (img.shape[0] + img.shape[1]) / 2) + 1  # line/font thickness
        
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
        
        if not opt.nobbox:
            cv2.rectangle(img, (x1, y1), (x2, y2), color, tl)

        if not opt.nolabel:
            label_parts = []
            
            # Add ID and class
            if identities is not None:
                base_label = f"{id}:{names[cat]}"
                
                # Add direction label if available
                if bee_stats is not None and id in bee_stats.tracks:
                    track_data = bee_stats.tracks[id]
                    if track_data["is_wasp"]:
                        base_label += " [WASP!]"
                    elif track_data["direction"] == "entering":
                        base_label += " [IN]"
                    elif track_data["direction"] == "leaving":
                        base_label += " [OUT]"
                
                label_parts.append(base_label)
            else:
                label_parts.append(f'{names[cat]} {confidences[i]:.2f}')
            
            label = " ".join(label_parts)
            
            tf = max(tl - 1, 1)  # font thickness
            t_size = cv2.getTextSize(label, 0, fontScale=tl / 3, thickness=tf)[0]
            c2 = x1 + t_size[0], y1 - t_size[1] - 3
            cv2.rectangle(img, (x1, y1), c2, color, -1, cv2.LINE_AA)  # filled
            cv2.putText(img, label, (x1, y1 - 2), 0, tl / 3, [225, 255, 255], thickness=tf, lineType=cv2.LINE_AA)

    # Add overall stats to the frame
    if bee_stats is not None:
        stats_text = [
            f"Total Bees: {len(bee_stats.unique_bees)}",
            f"Entering Hive: {bee_stats.entering_hive}",
            f"Leaving Hive: {bee_stats.leaving_hive}",
            f"Potential Wasps: {bee_stats.potential_wasps}"
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


def detect(save_img=False):
    source, weights, view_img, save_txt, imgsz, trace = opt.source, opt.weights, opt.view_img, opt.save_txt, opt.img_size, not opt.no_trace
    save_img = not opt.nosave and not source.endswith('.txt') and not opt.headless  # save inference images
    webcam = source.isnumeric() or source.endswith('.txt') or source.lower().startswith(
        ('rtsp://', 'rtmp://', 'http://', 'https://'))
    save_dir = Path(increment_path(Path(opt.project) / opt.name, exist_ok=opt.exist_ok))  # increment run
    if not opt.nosave:  
        (save_dir / 'labels' if save_txt else save_dir).mkdir(parents=True, exist_ok=True)  # make dir

    # Initialize
    set_logging()
    device = select_device(opt.device)
    half = device.type != 'cpu'  # half precision only supported on CUDA

    # Load model
    model = attempt_load(weights, map_location=device)  # load FP32 model
    stride = int(model.stride.max())  # model stride
    imgsz = check_img_size(imgsz, s=stride)  # check img_size

    if trace:
        model = TracedModel(model, device, opt.img_size)

    if half:
        model.half()  # to FP16

    # Second-stage classifier
    classify = False
    if classify:
        modelc = load_classifier(name='resnet101', n=2)  # initialize
        modelc.load_state_dict(torch.load('weights/resnet101.pt', map_location=device)['model']).to(device).eval()

    # Set Dataloader
    vid_path, vid_writer = None, None
    imgsz = check_img_size(imgsz, s=stride)  # Ensure image size is consistent
    if webcam:
        view_img = check_imshow()
        cudnn.benchmark = True  # set True to speed up constant image size inference
        dataset = LoadStreams(source, img_size=imgsz, stride=stride)
    else:
        dataset = LoadImages(source, img_size=imgsz, stride=stride)

    # Get names and colors
    names = model.module.names if hasattr(model, 'module') else model.names
    colors = [[random.randint(0, 255) for _ in range(3)] for _ in names]

    # Initialize bee statistics
    bee_stats = BeeStats()
    
    # For saving video with stats
    csv_path = None
    if opt.save_stats and not opt.nosave:
        csv_path = str(save_dir / 'bee_stats.csv')
        with open(csv_path, 'w') as f:
            f.write('timestamp,frame,total_bees,entering_hive,leaving_hive,potential_wasps\n')

    # For headless mode, print header
    if opt.headless:
        if opt.output_format == 'csv':
            print("type,timestamp,frame,total_bees,entering_hive,leaving_hive,potential_wasps")
        elif opt.output_format == 'json':
            import json
            # JSON header not needed
        else:  # text format
            print("# Bee Monitoring System - Headless Mode")
            print("# Format: [TYPE] [TIMESTAMP] [STATS/ALERT DETAILS]")

    # Run inference
    if device.type != 'cpu':
        model(torch.zeros(1, 3, imgsz, imgsz).to(device).type_as(next(model.parameters())))  # run once
    old_img_w = old_img_h = imgsz
    old_img_b = 1

    t0 = time.time()
    ###################################
    startTime = 0
    frame_count = 0
    ###################################
    for path, img, im0s, vid_cap in dataset:
        frame_count += 1
        
        if frame_count % 2 != 0:  # Process every 2nd frame
            continue
    
        img = torch.from_numpy(img).to(device)
        img = img.half() if half else img.float()  # uint8 to fp16/32
        img /= 255.0  # 0 - 255 to 0.0 - 1.0
        if img.ndimension() == 3:
            img = img.unsqueeze(0)

        # Warmup
        if device.type != 'cpu' and (old_img_b != img.shape[0] or old_img_h != img.shape[2] or old_img_w != img.shape[3]):
            old_img_b = img.shape[0]
            old_img_h = img.shape[2]
            old_img_w = img.shape[3]
            for i in range(3):
                model(img, augment=opt.augment)[0]

        # Inference
        t1 = time_synchronized()
        pred = model(img, augment=opt.augment)[0]
        t2 = time_synchronized()

        # Apply NMS
        pred = non_max_suppression(pred, opt.conf_thres, opt.iou_thres, classes=opt.classes, agnostic=opt.agnostic_nms)
        t3 = time_synchronized()

        # Apply Classifier
        if classify:
            pred = apply_classifier(pred, modelc, img, im0s)

        # Check if it's time to push to Firebase
        if opt.firebase:
            bee_stats.check_and_push_firebase()

        # Process detections
        for i, det in enumerate(pred):  # detections per image
            if webcam:  # batch_size >= 1
                p, s, im0, frame = path[i], '%g: ' % i, im0s[i].copy(), dataset.count
            else:
                p, s, im0, frame = path, '', im0s, getattr(dataset, 'frame', 0)

            p = Path(p)  # to Path
            save_path = str(save_dir / p.name)  # img.jpg
            txt_path = str(save_dir / 'labels' / p.stem) + ('' if dataset.mode == 'image' else f'_{frame}')  # img.txt
            gn = torch.tensor(im0.shape)[[1, 0, 1, 0]]  # normalization gain whwh
            if len(det):
                # Rescale boxes from img_size to im0 size
                det[:, :4] = scale_coords(img.shape[2:], det[:, :4], im0.shape).round()

                # Print results
                for c in det[:, -1].unique():
                    n = (det[:, -1] == c).sum()  # detections per class
                    s += f"{n} {names[int(c)]}{'s' * (n > 1)}, "  # add to string

                dets_to_sort = np.empty((0,6))
                # NOTE: We send in detected object class too
                for x1,y1,x2,y2,conf,detclass in det.cpu().detach().numpy():
                    dets_to_sort = np.vstack((dets_to_sort, 
                                np.array([x1, y1, x2, y2, conf, detclass])))

                if opt.track:
                    tracked_dets = sort_tracker.update(dets_to_sort, opt.unique_track_color)
                    tracks = sort_tracker.getTrackers()

                    # Update bee statistics
                    bee_stats.update_stats(im0.shape[0])

                    # draw boxes for visualization
                    if len(tracked_dets)>0:
                        bbox_xyxy = tracked_dets[:,:4]
                        identities = tracked_dets[:, 8]
                        categories = tracked_dets[:, 4]
                        confidences = None

                        if opt.show_track:
                            #loop over tracks
                            for t, track in enumerate(tracks):
                                track_color = colors[int(track.detclass)] if not opt.unique_track_color else sort_tracker.color_list[t]
                                
                                # Check if this track is a potential wasp
                                if track.id in bee_stats.tracks and bee_stats.tracks[track.id]["is_wasp"]:
                                    track_color = (0, 0, 255)  # Red for wasps
                                elif track.id in bee_stats.tracks and bee_stats.tracks[track.id]["direction"] == "entering":
                                    track_color = (0, 255, 0)  # Green for entering
                                elif track.id in bee_stats.tracks and bee_stats.tracks[track.id]["direction"] == "leaving":
                                    track_color = (255, 0, 0)  # Blue for leaving
                                
                                [cv2.line(im0, (int(track.centroidarr[i][0]),
                                                int(track.centroidarr[i][1])), 
                                                (int(track.centroidarr[i+1][0]),
                                                int(track.centroidarr[i+1][1])),
                                                track_color, thickness=opt.thickness) 
                                                for i,_ in  enumerate(track.centroidarr) 
                                                    if i < len(track.centroidarr)-1 ] 
                else:
                    bbox_xyxy = dets_to_sort[:,:4]
                    identities = None
                    categories = dets_to_sort[:, 5]
                    confidences = dets_to_sort[:, 4]
                
                im0 = draw_boxes(im0, bbox_xyxy, identities, categories, confidences, names, colors, bee_stats if opt.track else None)

            # Save stats to CSV if requested
            if opt.save_stats and not opt.nosave and csv_path:
                timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
                with open(csv_path, 'a') as f:
                    f.write(f'{timestamp},{frame_count},{len(bee_stats.unique_bees)},{bee_stats.entering_hive},{bee_stats.leaving_hive},{bee_stats.potential_wasps}\n')
                
            # Print time (inference + NMS) if not in headless mode
            if not opt.headless or opt.verbose:
                print(f'{s}Done. ({(1E3 * (t2 - t1)):.1f}ms) Inference, ({(1E3 * (t3 - t2)):.1f}ms) NMS')

            # Stream results
            ######################################################
            if dataset.mode != 'image' and opt.show_fps and not opt.headless:
                currentTime = time.time()
                fps = 1/(currentTime - startTime)
                startTime = currentTime
                cv2.putText(im0, "FPS: " + str(int(fps)), (20, 70), cv2.FONT_HERSHEY_PLAIN, 2, (0,255,0), 2)

            #######################################################
            # Only show video if view_img is true and not in headless mode
            if view_img and not opt.headless:
                cv2.imshow(str(p), im0)
                cv2.waitKey(1)  # 1 millisecond
                
            # In headless mode, output stats to console periodically
            if opt.headless and frame_count % opt.report_interval == 0:
                timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
                
                if opt.output_format == 'csv':
                    print(f"STATS,{timestamp},{frame_count},{len(bee_stats.unique_bees)},{bee_stats.entering_hive},{bee_stats.leaving_hive},{bee_stats.potential_wasps}")
                    # Alert for wasps if any new ones detected
                    if bee_stats.wasp_alerts > 0:
                        print(f"ALERT,{timestamp},WASP,{bee_stats.wasp_alerts}")
                
                elif opt.output_format == 'json':
                    import json
                    stats_data = {
                        "type": "STATS",
                        "timestamp": timestamp,
                        "frame": frame_count,
                        "total_bees": len(bee_stats.unique_bees),
                        "entering_hive": bee_stats.entering_hive,
                        "leaving_hive": bee_stats.leaving_hive,
                        "potential_wasps": bee_stats.potential_wasps
                    }
                    print(json.dumps(stats_data))
                    
                    # Alert for wasps if any new ones detected
                    if bee_stats.wasp_alerts > 0:
                        alert_data = {
                            "type": "ALERT",
                            "timestamp": timestamp,
                            "alert_type": "WASP",
                            "count": bee_stats.wasp_alerts
                        }
                        print(json.dumps(alert_data))
                
                else:  # text format
                    print(f"[STATS] {timestamp} - Bees: {len(bee_stats.unique_bees)} | Entering: {bee_stats.entering_hive} | Leaving: {bee_stats.leaving_hive} | Wasps: {bee_stats.potential_wasps}")
                    # Alert for wasps if any new ones detected
                    if bee_stats.wasp_alerts > 0:
                        print(f"[ALERT] {timestamp} - {bee_stats.wasp_alerts} potential wasp(s) detected!")
                
                # Reset alert counter after reporting
                if bee_stats.wasp_alerts > 0:
                    bee_stats.wasp_alerts = 0

            # Save results (image with detections)
            if save_img:
                if dataset.mode == 'image':
                    cv2.imwrite(save_path, im0)
                    print(f" The image with the result is saved in: {save_path}")
                else:  # 'video' or 'stream'
                    if vid_path != save_path:  # new video
                        vid_path = save_path
                        if isinstance(vid_writer, cv2.VideoWriter):
                            vid_writer.release()  # release previous video writer
                        if vid_cap:  # video
                            fps = vid_cap.get(cv2.CAP_PROP_FPS)
                            w = int(vid_cap.get(cv2.CAP_PROP_FRAME_WIDTH))
                            h = int(vid_cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
                        else:  # stream
                            fps, w, h = 30, im0.shape[1], im0.shape[0]
                            save_path += '.mp4'
                        vid_writer = cv2.VideoWriter(save_path, cv2.VideoWriter_fourcc(*'mp4v'), fps, (w, h))
                    vid_writer.write(im0)

    # Print final statistics
    timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
    
    # Final push to Firebase
    if opt.firebase:
        bee_stats.push_to_firebase()
    
    if opt.headless:
        # Output final stats in selected format
        if opt.output_format == 'csv':
            print(f"FINAL_STATS,{timestamp},{frame_count},{len(bee_stats.unique_bees)},{bee_stats.entering_hive},{bee_stats.leaving_hive},{bee_stats.potential_wasps}")
        
        elif opt.output_format == 'json':
            import json
            final_stats = {
                "type": "FINAL_STATS",
                "timestamp": timestamp,
                "frame": frame_count,
                "total_bees": len(bee_stats.unique_bees),
                "entering_hive": bee_stats.entering_hive,
                "leaving_hive": bee_stats.leaving_hive,
                "potential_wasps": bee_stats.potential_wasps,
                "processing_time": time.time() - t0
            }
            print(json.dumps(final_stats))
        
        else:  # text format
            print("\n===== Final Bee Monitoring Statistics =====")
            print(f"Timestamp: {timestamp}")
            print(f"Total unique bees detected: {len(bee_stats.unique_bees)}")
            print(f"Bees entering hive: {bee_stats.entering_hive}")
            print(f"Bees leaving hive: {bee_stats.leaving_hive}")
            print(f"Potential wasps detected: {bee_stats.potential_wasps}")
            print(f"Total processing time: {time.time() - t0:.3f}s")
            print("==========================================\n")
    else:
        print("\n===== Bee Monitoring Statistics =====")
        print(f"Total unique bees detected: {len(bee_stats.unique_bees)}")
        print(f"Bees entering hive: {bee_stats.entering_hive}")
        print(f"Bees leaving hive: {bee_stats.leaving_hive}")
        print(f"Potential wasps detected: {bee_stats.potential_wasps}")
        print("=====================================\n")

    if save_txt or save_img:
        s = f"\n{len(list(save_dir.glob('labels/*.txt')))} labels saved to {save_dir / 'labels'}" if save_txt else ''
        if not opt.headless or opt.verbose:
            print(f"Results saved to {save_dir}{s}")

    print(f'Done. ({time.time() - t0:.3f}s)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--weights', nargs='+', type=str, default='yolov7.pt', help='model.pt path(s)')
    parser.add_argument('--source', type=str, default='inference/images', help='source')  # file/folder, 0 for webcam
    parser.add_argument('--firebase', action='store_true', help='push data to Firebase')  #for firebase push
    parser.add_argument('--img-size', type=int, default=640, help='inference size (pixels)')
    parser.add_argument('--conf-thres', type=float, default=0.25, help='object confidence threshold')
    parser.add_argument('--iou-thres', type=float, default=0.45, help='IOU threshold for NMS')
    parser.add_argument('--device', default='', help='cuda device, i.e. 0 or 0,1,2,3 or cpu')
    parser.add_argument('--view-img', action='store_true', help='display results')
    parser.add_argument('--save-txt', action='store_true', help='save results to *.txt')
    parser.add_argument('--save-conf', action='store_true', help='save confidences in --save-txt labels')
    parser.add_argument('--nosave', action='store_true', help='do not save images/videos')
    parser.add_argument('--classes', nargs='+', type=int, help='filter by class: --class 0, or --class 0 2 3')
    parser.add_argument('--agnostic-nms', action='store_true', help='class-agnostic NMS')
    parser.add_argument('--augment', action='store_true', help='augmented inference')
    parser.add_argument('--update', action='store_true', help='update all models')
    parser.add_argument('--project', default='runs/detect', help='save results to project/name')
    parser.add_argument('--name', default='exp', help='save results to project/name')
    parser.add_argument('--exist-ok', action='store_true', help='existing project/name ok, do not increment')
    parser.add_argument('--no-trace', action='store_true', help='don`t trace model')

    parser.add_argument('--track', action='store_true', help='run tracking')
    parser.add_argument('--show-track', action='store_true', help='show tracked path')
    parser.add_argument('--show-fps', action='store_true', help='show fps')
    parser.add_argument('--thickness', type=int, default=2, help='bounding box and font size thickness')
    parser.add_argument('--seed', type=int, default=1, help='random seed to control bbox colors')
    parser.add_argument('--nobbox', action='store_true', help='don`t show bounding box')
    parser.add_argument('--nolabel', action='store_true', help='don`t show label')
    parser.add_argument('--unique-track-color', action='store_true', help='show each track in unique color')
    
    # New arguments for bee monitoring
    parser.add_argument('--save-stats', action='store_true', help='save bee statistics to CSV')
    parser.add_argument('--wasp-alert-method', type=str, default='visual', 
                        choices=['visual', 'log', 'both'], 
                        help='method to alert for wasps: visual, log, or both')
    parser.add_argument('--headless', action='store_true', help='run in headless mode (no GUI) for Raspberry Pi')
    parser.add_argument('--verbose', action='store_true', help='enable verbose output even in headless mode')
    parser.add_argument('--report-interval', type=int, default=100, 
                        help='interval (in frames) to report statistics in headless mode')
    parser.add_argument('--output-format', type=str, default='csv', choices=['csv', 'json', 'text'],
                        help='format for outputting statistics in headless mode')

    opt = parser.parse_args()
    print(opt)
    np.random.seed(opt.seed)

    sort_tracker = Sort(max_age=5, min_hits=2, iou_threshold=0.3)

    #check_requirements(exclude=('pycocotools', 'thop'))

    with torch.no_grad():
        if opt.update:  # update all models (to fix SourceChangeWarning)
            for opt.weights in ['yolov7.pt']:
                detect()
                strip_optimizer(opt.weights)
        else:
            detect()