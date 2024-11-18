# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|
    config.vm.box = "generic/ubuntu1704"
    config.vm.box_version = "4.3.12"

    config.vm.network "private_network", ip: "192.168.33.10"

    config.vm.provider "virtualbox" do |vb|
    #   vb.customize ["setextradata", :id, "VBoxInternal2/SharedFoldersEnableSymlinksCreate/v-root", "1"]  # This is for Windows host
      # Customize the amount of memory on the VM:
      vb.memory = "2048"
      vb.cpus = 2
    end

    config.vm.provision "shell", inline: <<-SHELL
      sudo apt update
      sudo apt -y install git make gcc
      git clone https://github.com/misachi/ext0fs.git
      export VAGRANT_HOME=/home/vagrant/
      dd if=/dev/zero of=$VAGRANT_HOME/test.img bs=1M count=500
      sudo losetup /dev/loop9 $VAGRANT_HOME/test.img
      cd $VAGRANT_HOME/ext0fs
      sudo make run EXT0_TMP=$VAGRANT_HOME MOUNT_POINT=$VAGRANT_HOME/testdir
      sudo make mount EXT0_TMP=$VAGRANT_HOME LOOP_DEV=/dev/loop9
    SHELL
  end