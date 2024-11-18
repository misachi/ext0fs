# EXT0-fs
Filesystem based on extended filesystem. EXT0fs does not have full functionality provided by production-level filesystems, and does not intend to. This is for educational purposes only. 

EXT0-fs has been ran on an older linux version(v4.15). The VM base image in the Vagrantfile already runs this version. It has also been ran on linux v6.2.

The entire filesystem is mapped into block groups of fixed sizes. Each group has the superblock as the first block. The block descriptor follows the superblock, which is then followed by the inode block. The bitmap block follows the inode block. Data blocks follow next. Each block group has 12 data blocks by default. The root directory is by default the 2 inode or block group.

There is only one inode per block group and one descriptor block per group. The superblock is at exactly 1024 bytes from the start of the device blocks/sector.

DO NOT run directly on your machine. This is so that you do not brick your system. The recommended way to install is inside a VM. A dummy Vagrantfile is provided to easily provision one locally.

## Setting up
### Using the provided Vagrantfile to run the setup
```
vagrant up  # when ran the first time
```

## Issues with setting up
If you encounter an issue with networking as below
```
No renderers compatible with netplan are available on guest. Please install
a compatible renderer.
```

This can be fixed with the following steps

First, ssh into the VM
```
vagrant ssh
```
Next, enable `systemd-networkd` service
```
sudo service systemd-networkd start
sudo systemctl systemd-networkd enable
exit # exit to host
```

Lastly, restart the VM
```
vagrant reload
```

Append the `--provision` flag if you need to re-run the setup commands

## Uninstallation
To uninstall the filesystem(normal user)
```
sudo make unmount
sudo make uninstall
```
