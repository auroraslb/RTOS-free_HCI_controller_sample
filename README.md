RTOS-free HCI controller sample
===============================

The goal of this sample is to provide a sample application demonstrating how to integrate the SoftDevice Controller and MPSL into an RTOS-free environment.


Set-up
------
To set up, and run the sample, begin by initializing west in your repository.
```
west init -m https://github.com/auroraslb/RTOS-free_HCI_controller_sample/
```
Then call
```
west update
```

You should now have all the necessary repositories to build and run the sample. The sample can be built using the following commands:
```
cmake -GNinja -Bbuild
cmake --build build/.
```