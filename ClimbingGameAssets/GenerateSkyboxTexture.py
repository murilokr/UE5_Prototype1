from PIL import Image, ImageOps
import os

def paste_image(input_image, output_image, position, flip_horizontal=False, rotate_180=False):
    # Flip horizontally if specified
    if flip_horizontal:
        input_image = ImageOps.mirror(input_image)
        
    # Rotate 180 degrees if specified
    if rotate_180:
        input_image = input_image.rotate(180)
        
    # Ensure input image has an alpha channel for transparency
    input_image = input_image.convert("RGBA")
    
    # Paste the input image onto the output image at the specified position
    output_image.paste(input_image, position, input_image)

def process_images(layer_image_paths, individual_image_paths, layer_position, individual_positions, output_image_path):
    # Check if the output image file already exists
    if os.path.exists(output_image_path):
        # Open existing output image for editing
        output_image = Image.open(output_image_path).convert("RGBA")
    else:
        # Create the output image with the desired size (2048x2048)
        output_image = Image.new('RGBA', (2048, 2048))
    
    # Process layer images
    if layer_image_paths:
        # Create a temporary layer image
        temp_layer_image = Image.new('RGBA', (2048, 512))
        
        # Iterate over layer image paths and paste onto temp_layer_image
        for i, image_path in enumerate(layer_image_paths):
            input_image = Image.open(image_path)
            position = ((512 * i), 0) # (0, 0)
            paste_image(input_image, temp_layer_image, position, flip_horizontal=True)
        
        # Paste the entire layer onto the output image
        output_image.paste(temp_layer_image, layer_position)
    
    # Process individual images
    if individual_image_paths:
        # Iterate over individual image paths and paste onto output_image
        for i, image_path in enumerate(individual_image_paths):
            input_image = Image.open(image_path)
            position = individual_positions[i]
            
            # Flip horizontally and rotate 180 degrees before pasting
            paste_image(input_image, output_image, position, flip_horizontal=True, rotate_180=True)
    
    # Save the output image
    output_image.save(output_image_path)

# Example usage:
if __name__ == "__main__":
    layer_image_paths = ['Output/Sky_Back0001.bmp', 'Output/Sky_Left0001.bmp', 'Output/Sky_Front0001.bmp', 'Output/Sky_Right0001.bmp']
    layer_position = (0, 768)
    
    individual_image_paths = ['Output/Sky_Up0001.bmp', 'Output/Sky_Down0001.bmp']
    individual_positions = [(512, 256), (512, 1280)]
    
    output_image_path = 'Output/output_image.png'
    
    process_images(layer_image_paths, individual_image_paths, layer_position, individual_positions, output_image_path)
