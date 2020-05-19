# mmu2s-clone

Update of the MMU2 firmware for ramps from : https://github.com/cskozlowski/mmu2/blob/master/doc/BOM.md

Project on https://www.thingiverse.com/thing:3910546

Now work with :
* MMU2S
* SKR mini v1.1 
- GT2560 board - configuration deleted
+ USB Mass storage device for firmware upload

#TODO :
* storing settings on sd card

* describe new feature
* Wire schematic
* Marlin 2.0 configuration
* howto compile / use it

#SKR Mini v1.1 bootloader
mini v1.1 bootloader have issue https://github.com/bigtreetech/BIGTREETECH-SKR-MINI-V1.1/issues/33#issue-619712422
and must be replaced to Mini E3 version from https://github.com/bigtreetech/BIGTREETECH-SKR-MINI-V1.1/issues/7#issuecomment-515892603
SKR-E3-SIP-bootloader.bin added to files folder
