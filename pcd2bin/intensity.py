import numpy as np
import os
import argparse
from pypcd import pypcd

pcd_file = '/home/sriz/Documents/projects/truckscenes-devkit/Generated_files/Scene_0_pcd_files/000000.pcd'
pc = pypcd.PointCloud.from_path(pcd_file)
print(f'Fields in pcd file:',pc.fields)