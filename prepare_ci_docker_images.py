#!/bin/env python3
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

import datetime
import subprocess


class NotFoundException(Exception):
    pass


def get_image_path(registry, project, variant, tag='latest'):
    return f'{registry}/{project}:{tag}'


def retrieve_current_image(registry, project, variant):
    image = get_image_path(registry, project, variant)
    p = subprocess.run(['docker', 'pull', image])
    if p.returncode != 0:
        raise NotFoundException(f'image {image} does not exists')


def is_current_image_obsolete(registry, project, variant):
    image = get_image_path(registry, project, variant)
    p = subprocess.run(['docker', 'inspect', "-f '{{ .Created }}'", image],
                       stdout=subprocess.PIPE)
    img_date = p.stdout.decode().strip(" '\n")
    img_day = img_date.split('T')[0]

    today = datetime.datetime.today().strftime('%Y-%m-%d')

    return img_day != today


def build_image(registry, project, variant, tag, parent, rpms=None):
    with open('Dockerfile', 'w') as f:
        f.write(f'''# this is our parent image
FROM {parent}

# Set the working directory to /app
WORKDIR /app

# Install any needed packages specified in requirements.txt
RUN dnf upgrade -y ; dnf clean all
''')
        if rpms is not None:
            f.write(f'RUN {rpms} ; dnf clean all\n')

    image_latest = get_image_path(registry, project, variant)
    image_tagged = get_image_path(registry, project, variant, tag)

    subprocess.run(['docker', 'build', '--tag', image_latest,
                                       '--tag', image_tagged,
                                       '.'])


def bootstrap_image(registry, project, variant, tag, rpms):
    # parent is variant:latest
    # we need to provide which rpms to install
    build_image(registry, project, variant, tag, f'{variant}:latest', rpms)


def update_image(registry, project, variant, tag):
    # we are basing our updated image on top of the previous
    image_latest = get_image_path(registry, project, variant)

    build_image(registry, project, variant, tag, image_latest)


def main():
    registry = 'registry.freedesktop.org'
    project = 'bentiss/libinput'
    variant = 'fedora'
    tag = '123456'
    rpms = 'git gcc gcc-c++ meson check-devel libudev-devel libevdev-devel doxygen graphviz valgrind binutils libwacom-devel cairo-devel gtk3-devel glib2-devel mtdev-devel'
    try:
        retrieve_current_image(registry, project, variant)
    except NotFoundException:
        bootstrap_image(registry, project, variant, tag, rpms)
    else:
        if is_current_image_obsolete(registry, project, variant):
            update_image(registry, project, variant, tag)


if __name__ == '__main__':
    main()
