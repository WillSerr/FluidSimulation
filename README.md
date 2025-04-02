# ComputeFluidSimulation
 
After spending a lot of my time working with shader programming and GPU pipelines, most of the end results were relating to computer graphics. This project served as a quick dive into fluid simulation to demonstrate other applications of my experience in GPU programming. 

The program builds upon the DirectX mini-engine as it provides a lightweight rendering pipeline as a starting point for implementing new shaders.  Simulation updates on compute shaders happen in four discrete stages: fluid advection, Sort by position key, Density re-calculation, and finally updating velocity. There are a few more improvements I want to make like an alternate rendering style. The most recent update was implementing a spatial HashMap to increase the current limit (for my hardware) of 32k to 131k particles at 30fps.

![Screenshot of swirling particles](/Screenshot.png)

Inside the MiniEngine you will find it's respective Readme file