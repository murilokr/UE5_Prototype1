import argparse

terragen_unit_by_meter = 0.067

def generate_terragen_campos_sequence(initial_campos, meters_per_frame, num_frames):
    # Parse initial_campos to get initial values
    campos_parts = initial_campos.split(',')
    x = int(campos_parts[0].strip())
    y = int(campos_parts[1].strip())
    z = float(campos_parts[2].strip())
    
    # Initialize list to store generated lines
    lines = []
    
    # Generate lines with increasing z value
    for i in range(num_frames):
        # Format the new line with updated z value
        new_line = f"CamPos {x}, {y}, {z:.3f}"
        lines.append(new_line)
        lines.append("frend")
        
        # Increase z by meters_per_frame in terragen units for the next iteration
        z += meters_per_frame * terragen_unit_by_meter
    return lines

if __name__ == "__main__":
    # Set up argparse to handle command line arguments
    parser = argparse.ArgumentParser(description='Generate sequence of CamPos lines with increasing z value.')
    
    # Default CamPos parameters
    parser.add_argument('initial_campos', nargs='?', type=str, default='164, 223, 45.647', help='Initial CamPos value as "x, y, z"')
    
    # Number of frames to generate
    parser.add_argument('num_frames', nargs='?', type=int, default=100, help='Number of frames to generate')
    
    # Meters per frame
    parser.add_argument('meters_per_frame', nargs='?', type=int, default='1', help='How many meters we will render per frame')
    
    
    #parser.add_argument('--output-file', '-o', type=str, default='generated_sequence.txt', help='Output file name (default: generated_sequence.txt)')
    
    # Parse command line arguments
    args = parser.parse_args()

    # Generate the sequence of lines
    sequence = generate_terragen_campos_sequence(args.initial_campos, args.meters_per_frame, args.num_frames)

    # Print each line in the sequence
    for line in sequence:
        print(line)
