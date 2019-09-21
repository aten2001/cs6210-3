There are a lot of ways to create a virtual machine using kvm in linux. I will introduce the one I think is the most convenient.

### Assumptions:
1. Ubuntu 14.04 or later.
2. Native system (physical machine).

### Step by Step:
1. install qemu-kvm, libvirt-bin and check if hardware supports the necessary virtualization extesions for KVM.

    ``` shell
    sudo apt install qemu-kvm libvirt-bin
    kvm-ok
    ```

2. install uvtool and uvtool-libvirt

    ``` shell
    sudo apt -y install uvtool
    ```

3. synchronize one specific cloud-image. Use _xenial(16.04)_ for example here.

    ``` shell
    uvt-simplestreams-libvirt sync release=xenial arch=amd64
    ```

4. create ssh key if you don't have one.

    ``` shell
    ssh-keygen
    ```

5. create a new virtual machine

    ``` shell
    uvt-kvm create firsttest release=xenial
    ```

6. connect to the running VM

    ``` shell
    uvt-kvm ssh firsttest --insecure
    ```

### Reference:
[https://help.ubuntu.com/lts/serverguide/virtualization.html](https://help.ubuntu.com/lts/serverguide/virtualization.html)

### Other options:
1. qemu-kvm: directly using qemu-kvm gives you flexibility, but you need setup everything yourself.
2. virt-manager: a gui-based tool, very intuitive to use, but you need GUI somewhere.
3. ...


