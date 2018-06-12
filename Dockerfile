# Use an official Python runtime as a parent image
FROM fedora:latest

# Set the working directory to /app
WORKDIR /app

# Copy the current directory contents into the container at /app
ADD . /app

# Install any needed packages specified in requirements.txt
RUN dnf upgrade -y
RUN dnf install -y git gcc gcc-c++ meson check-devel libudev-devel libevdev-devel doxygen graphviz valgrind binutils libwacom-devel cairo-devel gtk3-devel glib2-devel mtdev-devel

# Make port 80 available to the world outside this container
# EXPOSE 80

# Define environment variable
# ENV NAME World

# Run app.py when the container launches
CMD ["bash"]
