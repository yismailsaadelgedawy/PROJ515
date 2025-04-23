#!/usr/bin/env python3
import threading
import time
import subprocess
import sys
import os
import signal
import argparse

# Import the main functions from both systems
# If they're in different directories, adjust the import paths as needed
try:
    from thread_1_cam import detect_and_monitor
    from thread_2_spi import main as health_monitor_main
    DIRECT_IMPORT = True
except ImportError:
    DIRECT_IMPORT = False
    print("Could not directly import monitor modules, will use subprocess mode")

# Global flags to control threads
running = True
processes = []

def run_bee_monitor(args):
    """
    Thread function to run the bee monitoring system
    """
    print("[BEE MONITOR] Starting bee monitoring thread...")
    
    if DIRECT_IMPORT:
        # Run directly if imported successfully
        try:
            detect_and_monitor()
        except Exception as e:
            print(f"[BEE MONITOR] Error: {e}")
    else:
        # Run as subprocess
        cmd = [sys.executable, "thread_1_cam.py"]
        
        # Add command line arguments
        if args.source:
            cmd.extend(["--source", args.source])
        if args.weights:
            cmd.extend(["--weights", args.weights])
        if args.device:
            cmd.extend(["--device", args.device])
        if args.record_length:
            cmd.extend(["--record-length", str(args.record_length)])
        if args.fps:
            cmd.extend(["--fps", str(args.fps)])
        
        try:
            print(f"[BEE MONITOR] Running command: {' '.join(cmd)}")
            process = subprocess.Popen(cmd)
            processes.append(process)
            process.wait()
        except Exception as e:
            print(f"[BEE MONITOR] Error starting process: {e}")

def run_health_monitor():
    """
    Thread function to run the health monitoring system
    """
    print("[HEALTH MONITOR] Starting health monitoring thread...")
    
    if DIRECT_IMPORT:
        # Run directly if imported successfully
        try:
            health_monitor_main()
        except Exception as e:
            print(f"[HEALTH MONITOR] Error: {e}")
    else:
        # Run as subprocess
        cmd = [sys.executable, "thread_2_spi.py"]
        try:
            print(f"[HEALTH MONITOR] Running command: {' '.join(cmd)}")
            process = subprocess.Popen(cmd)
            processes.append(process)
            process.wait()
        except Exception as e:
            print(f"[HEALTH MONITOR] Error starting process: {e}")

def signal_handler(sig, frame):
    """
    Handle Ctrl+C to gracefully shut down all threads and processes
    """
    global running
    print("\nShutting down bee monitoring system...")
    running = False
    
    for process in processes:
        try:
            process.terminate()
            print(f"Terminated subprocess PID {process.pid}")
        except:
            pass
    
    sys.exit(0)

def main():
    """
    Main coordinator function
    """
    parser = argparse.ArgumentParser(description="Multithreaded Bee Monitoring System")
    parser.add_argument("--source", type=str, help="Camera source (e.g., 16 for IP camera)")
    parser.add_argument("--weights", type=str, help="Path to YOLOv7 weights")
    parser.add_argument("--device", type=str, default="cpu", help="Device to run on (cpu, 0, 1, etc.)")
    parser.add_argument("--record-length", type=int, default=60, help="Recording length in seconds")
    parser.add_argument("--fps", type=int, default=10, help="FPS for recording")
    
    args = parser.parse_args()
    
    print("Starting Multithreaded Bee Monitoring System")
    print("Press Ctrl+C to stop all monitoring")
    
    # Set up signal handler for graceful shutdown
    signal.signal(signal.SIGINT, signal_handler)
    
    # Create and start the monitoring threads
    bee_thread = threading.Thread(target=run_bee_monitor, args=(args,))
    health_thread = threading.Thread(target=run_health_monitor)
    
    bee_thread.daemon = True
    health_thread.daemon = True
    
    bee_thread.start()
    # Add a small delay to prevent both systems from accessing camera at the same time
    time.sleep(2)
    health_thread.start()
    
    try:
        # Keep the main thread alive to handle Ctrl+C
        while running:
            time.sleep(1)
            
            # Check if threads are still alive
            if not bee_thread.is_alive():
                print("[COORDINATOR] Bee monitoring thread has stopped, restarting...")
                bee_thread = threading.Thread(target=run_bee_monitor, args=(args,))
                bee_thread.daemon = True
                bee_thread.start()
            
            if not health_thread.is_alive():
                print("[COORDINATOR] Health monitoring thread has stopped, restarting...")
                health_thread = threading.Thread(target=run_health_monitor)
                health_thread.daemon = True
                health_thread.start()
                
    except KeyboardInterrupt:
        signal_handler(signal.SIGINT, None)

if __name__ == "__main__":
    main()
