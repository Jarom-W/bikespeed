#Bike Dashboard Files

This device connects to your phone's hotspot to serve a simple dashboard over http straight to your phone's browser giving you realtime updates on your
speed, trip, and odometer. It's fully open source so you can customize it any way you'd like.

Required hardware to build what you see in the video:

-1x XAIO ESP32 C3 microcontroller board.
-1x MPU 9250 Gyro module
-2x 100k Ohm resistor
-1x 0.1 uF capacitor
-1x Simple electrical switch (I only had a triple switch on hand but all the switches are connected. A single switch works perfectly)
-1x 3.7V 1000 mAh LiPo Lithium Ion Battery

Wires, Soldering Equipment, and a board to assemble everything onto.+

##Files in this repository:

- 3D_print_files.zip -> All the files to print the enclosure. I used 4 nuts and 4 screws to assemble the clamps. I bought them here:

https://www.amazon.com/dp/B0DQRG2NFC?ref=ppx_yo2ov_dt_b_fed_asin_title

https://www.amazon.com/dp/B0BLBKSZXJ?ref=ppx_yo2ov_dt_b_fed_asin_title

- bikespeed.kicad_sch -> KiCad schematic explaining the electrical connections which are hard to see in the assembled board.
- firmware.ino -> Single Arduino IDE file containing the entire firmware. You'll need to open it and add your own phone's hotspot SSID and password.
