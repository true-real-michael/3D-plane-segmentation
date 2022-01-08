#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/io/pcd_io.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <math.h>
#include <string>

int
main(int argc, char **argv)
{
  if (argc != 5)
  {
    std::cerr << "usage: origin2nan <direction> <prefix> <start> <end>.  \n";
  }

  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>::Ptr output (new pcl::PointCloud<pcl::PointXYZ>);


  std::string prefix = std::string(argv[1]) + std::string(argv[2]); //("/media/work/datasets/bremen_city/pcd/scan");
  int start_index = atoi(argv[3]);
  int end_index = atoi(argv[4]);

  std::string pcd_file;

  pcl::PCDReader reader;
  pcl::PCDWriter writer;

  for (int index = start_index; index <= end_index; index++)
  {
    char buf[5];
    sprintf(buf, "%03d", index);
    pcd_file = prefix + std::string(buf) + ".pcd";

    if (reader.read(pcd_file, *cloud) == -1)
    {
      PCL_ERROR ("Couldn't read file %s!\n", pcd_file.c_str());
      return (-1);
    }
    PCL_INFO ("Loaded %d points: (width %d, height: %d) from %s.\n",
              cloud->points.size(), cloud->width, cloud->height, pcd_file.c_str());

    int invalid = 0;
    for (size_t i = 0; i < cloud->size(); i++)
    {
      if (cloud->points[i].x == 0. && cloud->points[i].y == 0. && cloud->points[i].z == 0.)
        invalid ++;
    }
    std::cout << "there are " << invalid << " invalid points.\n";

    pcl::copyPointCloud(*cloud, *output);

    int height = cloud->height;
    int width = cloud->width;
    for (size_t i = 0; i < cloud->size(); i++)
    {
      if (cloud->points[i].x == 0. && cloud->points[i].y == 0. && cloud->points[i].z == 0.)
        output->points[i].x = output->points[i].y = output->points[i].z = NAN;
    }
    int nans = 0;
    for (size_t i = 0; i < output->size(); i++)
    {
      if (std::isnan(output->points[i].x) || std::isnan(output->points[i].y) || std::isnan(output->points[i].z))
        nans ++;
    }
    //output->height = height;
    //output->width = width;
    //output->is_dense = false;
    std::cout << "there are " << nans << " NANs.\n";
    std::cout << "width :" << output->width << "; height: " << output->height << std::endl;
    writer.writeBinary (pcd_file, *output);
  }

  return 0;
}







