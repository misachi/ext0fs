# EXT0-fs
Filesystem based on extended filesystem. EXT0fs does not have full functionality provided by production-level filesystems, and does not intend to. This is for educational purposes only. 

EXT0-fs has been ran on an older linux version(v4.15). The VM base image in the Vagrantfile already runs this version. It has also been ran on linux v6.2.

The entire filesystem is mapped into block groups of fixed sizes. Each group has the superblock as the first block. The block descriptor follows the superblock, which is then followed by the inode block. The bitmap block follows the inode block. Data blocks follow next. Each block group has 12 data blocks by default. The root directory is by default the 2 inode or block group.

There is only one inode per block group and one descriptor block per group. The superblock is at exactly 1024 bytes from the start of the device blocks/sector.

DO NOT run directly on your machine. This is so that you do not brick your system. The recommended way to install is inside a VM. A dummy Vagrantfile is provided to easily provision one locally.

## Setting up
Clone the source
```
git clone https://github.com/misachi/ext0fs.git
```

Create a file to map to our "pseudo device"
```
dd if=/dev/zero of=/tmp/test.img bs=1M count=100
```

By default `/dev/loop0` device is chosen. Run `losetup -f` to get an unused device and set it in the Makefile `LOOP_DEV`

Next steps will require `sudo` access, which makes provisioning a VM the best option:
```
sudo make run
sudo make mount
```

A directory `testdir` will be created in your current path. This is the mounted filesystem for EXT0-fs. To access it you will need to be root
```
sudo su
cd testdir
```

Once you are able to `cd` into it, you can play around with the filesystem
```
mkdir test
cd test
ls -la  # etc
```

## Uninstallation
To uninstall the filesystem
```
exit  # exit root -- Assuming you are still logged in as root
sudo make unmount
sudo make uninstall
```
