#!/bin/bash

sudo apt-get install ibus ibus-clutter ibus-gtk ibus-gtk3 ibus-qt4
im-config -s ibus  
sudo apt-get install ibus-pinyin  
sudo ibus-setup
