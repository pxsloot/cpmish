# -*- mode: ruby -*-
# vi: set ft=ruby :
#
# This builds a ubuntu cpmish development vm
# All necessary tools and libraries are provided
# The provision step installs dev tools and builds
# and install 'ack' from source. Default platform
# for ack is cpm.
#
# use vagrant if you can't or don't want to install
# all the necessary tools, libraries, compilers and
# what not on your workstation.
#
# Config is for virtualbox and libvirt hypervisors
#
# After running 'vagrant up' and 'vagrant ssh' you'll
# find this directory mounted on /vagrant. Go there and
# run 'make' to build the disk images, also available
# on the 'workstation' side.

# 1 cpu/512mb ram is a bit slim, but it'll build ack and
# cpmish without a hitch
vm_cpu=1
vm_mem=512
vm_name="cpmdev"
vm_timezone="Europe/Amsterdam"

Vagrant.configure("2") do |config|
  config.vm.define vm_name do |config|
    config.vm.box = "generic/ubuntu1604"

    # setup for virtualbox and libvirt
    config.vm.provider :virtualbox do |vbox, override|
      vbox.memory = vm_mem
      vbox.cpus   = vm_cpu
      override.vm.synced_folder ".", "/vagrant"
    end
    config.vm.provider :libvirt do |libvirt, override|
      override.vm.synced_folder ".", "/vagrant",
        :nfs_export  => true,
        :nfs         => true,
        :nfs_version => 4,
        :nfs_udp     => false
      libvirt.memory   = vm_mem
      libvirt.cpus     = vm_cpu
      libvirt.cpu_mode = 'host-passthrough'
      libvirt.nested   = true
    end

    # provisioning
    # install dev tools and compile & install ack
    config.vm.provision "shell", inline: <<-SHELL
      ln -sf /usr/share/zoneinfo/#{vm_timezone} /etc/localtime
      apt-get update
      apt-get install -y unzip curl man findutils git \
            gcc g++ make flex bison ninja-build \
            netpbm lua5.1 lua-posix libz80ex-dev \
            cpmtools libreadline-dev
      cd /tmp
      rm -rf ack ack-default ack-build ack.zip
      git clone --depth=1 https://github.com/davidgiven/ack.git
      cd ack
      sed -i 's/DEFAULT_PLATFORM = pc86/DEFAULT_PLATFORM = cpm/' Makefile
      make +ack-cpm && make install
    SHELL
  end
end
