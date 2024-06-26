from PIL import Image, ImageOps
import os

output_folder = '../Output/'
texture_name = "Sky_MountainSide_Alt_"


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

def process_images(skybox_layer_faces, individual_skybox_faces, layer_position, individual_positions, frame_number, output_image_path):
    # Check if the output image file already exists
    if os.path.exists(output_image_path):
        # Open existing output image for editing
        output_image = Image.open(output_image_path).convert("RGBA")
    else:
        # Create the output image with the desired size (2048x2048)
        output_image = Image.new('RGBA', (2048, 2048))
    
    # Process skybox layer faces
    if skybox_layer_faces:
        # Create a temporary layer image
        temp_layer_image = Image.new('RGBA', (2048, 512))
        
        # Iterate over layer skybox faces and paste onto temp_layer_image
        for i, skybox_face in enumerate(skybox_layer_faces):
            skybox_face_path = output_folder + "Sky_" + skybox_face + frame_number + ".bmp"
            input_image = Image.open(skybox_face_path)
            position = ((512 * i), 0) # (0, 0)
            paste_image(input_image, temp_layer_image, position, flip_horizontal=True)
        
        # Paste the entire layer onto the output image
        output_image.paste(temp_layer_image, layer_position)
    
    # Process individual images
    if individual_skybox_faces:
        # Iterate over individual skybox faces and paste onto output_image
        for i, skybox_face in enumerate(individual_skybox_faces):
            skybox_face_path = output_folder + "Sky_" + skybox_face + frame_number + ".bmp"
            input_image = Image.open(skybox_face_path)
            position = individual_positions[i]
            
            # Flip horizontally and rotate 180 degrees before pasting
            paste_image(input_image, output_image, position, flip_horizontal=True, rotate_180=True)
    
    # Save the output image
    output_image.save(output_image_path)



if __name__ == "__main__":
    skybox_layer_faces = ["Back", "Left", "Front", "Right"]
    layer_position = (0, 768)
    
    individual_skybox_faces = ["Up", "Down"]
    individual_positions = [(512, 256), (512, 1280)]
    
    frames = ["0001", "0002", "0003", "0004", "0005", "0006", "0007", "0008", "0009", "0010", "0011", "0012", "0013", "0014", "0015", "0016", "0017", "0018", "0019", "0020", "0021"]
    
    for frame_number in frames:
        output_image_path = "../Textures/" + texture_name + frame_number + ".png"
        process_images(skybox_layer_faces, individual_skybox_faces, layer_position, individual_positions, frame_number, output_image_path)