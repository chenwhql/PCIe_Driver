#!/bin/bash

sudo cp -r ~/Downloads/Font /usr/share/fonts/
sudo mkfontscale
sudo mkfontdir
sudo fc-cache -fv