/*
 * Software License Agreement (BSD License)
 *
 *  Technical Aspects of Multimodal Systems (TAMS) - http://tams-www.informatik.uni-hamburg.de/
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of TAMS, nor the names of its contributors may
 *     be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * Author : Junhao Xiao
 * Email  : junhao.xiao@ieee.org, xiao@informatik.uni-hamburg.de
 *
 */

#include "octree_region_growing_segmentation/octree_region_growing_segmentation.h"
#include "common/rgb.h"
#include <algorithm>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/visualization/point_cloud_handlers.h>
#include <time.h>
using namespace tams;

PlanarSegment::StdVectorPtr
OctreeRGSegmentation::segments()
{
  return planar_patches_;
}

void
OctreeRGSegmentation::segmentation()
{
  applySegmentation();
}

void
OctreeRGSegmentation::setParameters (OctreeRegionGrowingSegmentationParameters &parameters)
{
  sliding_sphere_size_ = parameters.sliding_sphere_size;
  max_neighbor_dis_ = parameters.max_neighbor_dis * parameters.max_neighbor_dis;
  max_point2plane_dis_ = parameters.max_point2plane_dis;
  max_angle_difference_ = cos(parameters.max_angle_difference * M_PI / 180) ;
  max_segment_mse_ = parameters.max_segment_mse;
  max_local_mse_ = parameters.max_local_mse;
  max_seed_mse_ = parameters.max_seed_mse;
  nearest_neighbor_size_ = parameters.nearest_neighbor_size;
  min_segment_size_ = parameters.min_segment_size;
  downsampling_ = parameters.downsampling;
  show_filtered_cloud_ = parameters.show_filtered_cloud;
  downsampling_leafsize_ = parameters.downsampling_leafsize;
  osr_mean_k_ = parameters.osr_mean_k;
  osr_StddevMulThresh_ = parameters.osr_StddevMulThresh;
}

//void
//OctreeRGSegmentation::setSensorNoiseModel(const double a0, const double a1, const double a2)
//{
//  polynomial_noise[0] = a0;
//  polynomial_noise[1] = a1;
//  polynomial_noise[2] = a2;
//}

void
OctreeRGSegmentation::setInput(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud)
{
  ognzd_cloud_ = cloud;
  int size = cloud->height * cloud->width;

  uognzd_indice_to_ognzd_.clear();
  uognzd_indice_to_ognzd_.resize(size, 0);
  uognzd_points_.clear ();
  uognzd_points_.resize (size, Eigen::Vector3d::Zero());
  ognzd_points_.clear();
  ognzd_points_.resize (size, Eigen::Vector3d::Zero());
  uognzd_cloud_->points.resize(size);
  int cnt = 0;
  for (int i = 0; i < size; i++)
  {
    if (std::isnan(ognzd_cloud_->points[i].x) || std::isnan(ognzd_cloud_->points[i].y) || std::isnan(ognzd_cloud_->points[i].z))
      continue;
    else
    {
      uognzd_cloud_->points[cnt] = ognzd_cloud_->points[i];
      uognzd_indice_to_ognzd_[cnt] = i;
      uognzd_points_[cnt] = Eigen::Vector3d(ognzd_cloud_->points[i].x,
                                            ognzd_cloud_->points[i].y,
                                            ognzd_cloud_->points[i].z);
      ognzd_points_[i] = Eigen::Vector3d(ognzd_cloud_->points[i].x,
                                         ognzd_cloud_->points[i].y,
                                         ognzd_cloud_->points[i].z);
      cnt ++;
    }
  }
  pcd_size_ = cnt;
  uognzd_cloud_->points.erase(uognzd_cloud_->points.begin()+cnt, uognzd_cloud_->points.end());
  uognzd_cloud_->height = 1;
  uognzd_cloud_->width = cnt;
  uognzd_points_.erase(uognzd_points_.begin() + cnt, uognzd_points_.end());
  uognzd_indice_to_ognzd_.erase(uognzd_indice_to_ognzd_.begin() + cnt, uognzd_indice_to_ognzd_.end());
  std::cout << "there are " << cnt << " valid points in the cloud.\n";
}

void
OctreeRGSegmentation::downsampling ()
{
  pcl::PointCloud<pcl::PointXYZ>::Ptr downsampled (new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>::Ptr filtered (new pcl::PointCloud<pcl::PointXYZ>);

  //downsampling the point cloud
  pcl::VoxelGrid<pcl::PointXYZ> voxel_grid_downsampling;
  voxel_grid_downsampling.setInputCloud (uognzd_cloud_);
  voxel_grid_downsampling.setLeafSize (downsampling_leafsize_, downsampling_leafsize_, downsampling_leafsize_);
  voxel_grid_downsampling.filter (*downsampled);
  std::cout << "cloud size after down-sampling: " << downsampled->size () << std::endl;
  if (show_filtered_cloud_)
  {
    pcl::visualization::PCLVisualizer downsampled_viewer ("Detected planes with Pseudo-color");
    downsampled_viewer.setBackgroundColor (0.0, 0.0, 0.0);
    downsampled_viewer.addPointCloud (downsampled, "point cloud", 0);
    downsampled_viewer.addCoordinateSystem ();
    downsampled_viewer.setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "point cloud");
    downsampled_viewer.spin();
  }
  // filtering the input point cloud
  pcl::StatisticalOutlierRemoval<pcl::PointXYZ> statistical_filter;
  statistical_filter.setInputCloud (downsampled);
  statistical_filter.setMeanK (osr_mean_k_);
  statistical_filter.setStddevMulThresh (osr_StddevMulThresh_);
  statistical_filter.filter (*filtered);
  std::cerr << "cloud size after filtering: " << filtered->size () << std::endl;
  if (show_filtered_cloud_)
  {
    pcl::visualization::PCLVisualizer filtered_viewer ("Detected planes with Pseudo-color");
    filtered_viewer.setBackgroundColor (0.0, 0.0, 0.0);
    filtered_viewer.addPointCloud (filtered, "point cloud", 0);
    filtered_viewer.addCoordinateSystem ();
    filtered_viewer.setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "point cloud");
    filtered_viewer.spin();
  }
  uognzd_cloud_ = filtered;
}

void
OctreeRGSegmentation::octreeCaching()
{
  if (!tree_)
  {
    tree_.reset(new pcl::search::KdTree<pcl::PointXYZ> (false));
    tree_->setInputCloud (uognzd_cloud_);
  }

  nn_indices_ = new int [pcd_size_ * nearest_neighbor_size_];
  nn_dis_ = new double [pcd_size_ * nearest_neighbor_size_];
  has_local_plane_ = new bool [pcd_size_];
  memset(has_local_plane_, false, pcd_size_);
  local_mse_ = new double [pcd_size_];
  memset(local_mse_, 0.0, pcd_size_);
  local_normals_.clear();
  local_normals_.resize(pcd_size_, Eigen::Vector3d::Zero());
  sliding_spheres_.clear ();
  sliding_spheres_.resize (pcd_size_);

  std::vector<int> indices (sliding_sphere_size_);
  std::vector<float> pointRadiusSquaredDistance (sliding_sphere_size_);
  for (int i = 0; i < pcd_size_; i++)
  {
    tree_->nearestKSearch(i, sliding_sphere_size_, indices, pointRadiusSquaredDistance);
    for (size_t j = 0; j < nearest_neighbor_size_; j++)
    {
      nn_indices_[i * nearest_neighbor_size_ + j] = indices[j + 1];
      nn_dis_[i * nearest_neighbor_size_ + j] = nn_dis_[j + 1];
    }
  }
}

void
OctreeRGSegmentation::slidingSphere()
{
  SlidingSphreItem tmp;
  Eigen::Vector3d sum = Eigen::Vector3d::Zero();
  Eigen::Matrix3d scatter_matrix = Eigen::Matrix3d::Zero();
  Eigen::Vector3d mass_center = Eigen::Vector3d::Zero();
  Eigen::EigenSolver<Matrix3d> eigensolver;
  Eigen::Vector3d eigenvalues = Eigen::Vector3d::Zero();
  Eigen::Matrix3d eigenvectors = Eigen::Matrix3d::Zero();
  int cnt = 0;
  int *begin = &(nn_indices_[0]);
  int *end, *pIndex;
  for (int i = 0; i < pcd_size_; i++)
  {
    end = begin + nearest_neighbor_size_;
    sum = uognzd_points_[i];
    for (pIndex = begin; pIndex < end; pIndex++)
    {
      sum += uognzd_points_[*pIndex];
    }
    mass_center = sum / static_cast<double > (sliding_sphere_size_);
    scatter_matrix = (uognzd_points_[i] - mass_center) * (uognzd_points_[i] - mass_center).transpose ();
    for (pIndex = begin; pIndex < end; pIndex++)
    {
      scatter_matrix += (uognzd_points_[*pIndex] - mass_center) * (uognzd_points_[*pIndex] - mass_center).transpose ();
    }
    eigensolver.compute(scatter_matrix);
    eigenvalues = eigensolver.eigenvalues().real();
    eigenvectors = eigensolver.eigenvectors().real();
    int min_eigenvalue_index;
    double lambda[2];
    double min_eigenvalue = eigenvalues.minCoeff(&min_eigenvalue_index);
    size_t lambda_index = 0;
    for (size_t q = 0; q < 3; q++)
    {
      if (q == min_eigenvalue_index)
        continue;
      lambda[lambda_index] = eigenvalues(q)/min_eigenvalue;
      lambda_index ++;
    }
    double min_lambda = std::min(lambda[0],lambda[1]);

    if (min_lambda * min_lambda > sliding_sphere_size_)
    {
      tmp.normal = eigenvectors.col(min_eigenvalue_index);
      if (tmp.normal.dot(mass_center) < 0)
        tmp.normal = -tmp.normal;
      tmp.mse = min_eigenvalue / sliding_sphere_size_;
      tmp.index = i;
      has_local_plane_[i] = true;
      local_mse_[i] = tmp.mse;
      local_normals_[i] = tmp.normal;
      sliding_spheres_[i] = tmp;
      cnt ++;
    }
    else
    {
      tmp.mse = 9999;
      sliding_spheres_[i] = tmp;
    }
    begin = end;
  }
//  PCL_INFO ("\nsliding windows finished: %d have been computed.\n", cnt);
}

void
OctreeRGSegmentation :: investigateNeighbors (int index)
{
  int start = index * nearest_neighbor_size_;
  int end = start + nearest_neighbor_size_;
  for (int i = start; i < end; i++)
  {
    if (visited_[nn_indices_[i]] || added_to_region_[nn_indices_[i]])
      continue;
    if (nn_dis_[i] < max_neighbor_dis_)
    {
      neighbor_points_.push_back(nn_indices_[i]);
      visited_[nn_indices_[i]] = true;
    }
  }
}

void
OctreeRGSegmentation :: applySegmentation ()
{
  if (downsampling_)
    downsampling ();
  planar_patches_->clear ();
  remained_points_.clear ();
  if (!tree_)
  {
    tree_.reset(new pcl::search::KdTree<pcl::PointXYZ> (false));
    tree_->setInputCloud (uognzd_cloud_);
  }
  visited_ = new bool [pcd_size_];
  added_to_region_ = new bool [pcd_size_];
  memset(added_to_region_, false, pcd_size_);
  Eigen::EigenSolver<Matrix3d> eigensolver;
  Eigen::Vector3d eigenvalues = Eigen::Vector3d::Zero();
  Eigen::Matrix3d eigenvectors = Eigen::Matrix3d::Zero();
  Eigen::Vector3d sum = Eigen::Vector3d::Zero();
  Eigen::Vector3d mass_center = Eigen::Vector3d::Zero();
  Eigen::Matrix3d scatter_matrix = Eigen::Matrix3d::Zero();
  badpoints_num_ = 0;
  slidingSphere();
  //sort (sliding_spheres_.begin(), sliding_spheres_.end());
  double min_dis = 1000;
  int seed_index = -1;
  Eigen::Vector3d seed(9.0, 0.1, 0.7);
  for (size_t i = 0; i< sliding_spheres_.size (); i++)
  {
    double dis = (uognzd_points_[sliding_spheres_[i].index] - seed).norm();
    if (dis < min_dis)
    {
      min_dis = dis;
      seed_index = i;
    }
  }

  PCL_INFO ("seed of T-shape found!\n");
  int index = seed_index;
  memset(visited_, false, pcd_size_);
  bool isSmall = true;
  PlanarSegment tmp_pp;
  neighbor_points_.clear();
  neighbor_points_.push_back(sliding_spheres_[index].index);
  visited_[sliding_spheres_[index].index] = true;
  added_to_region_[sliding_spheres_[index].index] = true;
  while (!neighbor_points_.empty())
  {
    int pos_index = neighbor_points_.front();
    neighbor_points_.erase(neighbor_points_.begin());
    Eigen::Vector3d point3d = uognzd_points_[pos_index];
    if (tmp_pp.point_num < 7)
    {
      tmp_pp.points.push_back(pos_index);
      tmp_pp.point_num ++;
      tmp_pp.sum += point3d;
      added_to_region_[pos_index] = true;
      investigateNeighbors(pos_index);
      isSmall = true;
    }
    else if (tmp_pp.point_num >= 7 && isSmall == true)
    {
      tmp_pp.mass_center = tmp_pp.sum / 7.0;
      for (int i = 0; i < static_cast<int>(tmp_pp.points.size()); i++)
      {
        tmp_pp.scatter_matrix += (uognzd_points_[tmp_pp.points[i]] - tmp_pp.mass_center) * (uognzd_points_[tmp_pp.points[i]] - tmp_pp.mass_center).transpose();
      }
      eigensolver.compute(tmp_pp.scatter_matrix);
      eigenvalues = eigensolver.eigenvalues().real();
      eigenvectors = eigensolver.eigenvectors().real();
      int min_eigenvalue_index = 100;
      double min_eigenvalue = eigenvalues.minCoeff(&min_eigenvalue_index);
      tmp_pp.normal = eigenvectors.col(min_eigenvalue_index);
      tmp_pp.bias = tmp_pp.normal.dot(tmp_pp.mass_center);
      tmp_pp.mse = min_eigenvalue / 7.0;
      if (tmp_pp.bias < 0)
      {
        tmp_pp.bias = -tmp_pp.bias;
        tmp_pp.normal = -tmp_pp.normal;
      }
      isSmall = false;
    }
    else if (tmp_pp.point_num >= 7 && isSmall == false)
    {
      sum = tmp_pp.sum + point3d;
      mass_center = sum / static_cast<double >(tmp_pp.point_num + 1);
      scatter_matrix = tmp_pp.scatter_matrix + point3d * point3d.transpose() - sum * mass_center.transpose() + tmp_pp.sum * tmp_pp.mass_center.transpose();
      eigensolver.compute(scatter_matrix);
      eigenvalues = eigensolver.eigenvalues().real();
      eigenvectors = eigensolver.eigenvectors().real();
      int min_eigenvalue_index;
      double min_eigenvalue = eigenvalues.minCoeff(&min_eigenvalue_index);
      PCL_DEBUG("index of the minimum eigenvalue: %d.\n", min_eigenvalue_index);
      if (min_eigenvalue / static_cast<double >(tmp_pp.point_num + 1) > max_segment_mse_)
      {
        visited_[pos_index] = false;
        continue;
      }
      if (fabs(eigenvectors.col(min_eigenvalue_index).dot(mass_center - point3d)) > max_point2plane_dis_)
      {
        visited_[pos_index] = false;
        continue;
      }
      if (has_local_plane_[pos_index] && local_mse_[pos_index] < max_local_mse_)
      {
        double dot_product = local_normals_[pos_index].dot(eigenvectors.col(min_eigenvalue_index));
        if ( fabs(dot_product) < max_angle_difference_)
        {
          visited_[pos_index] = false;
          continue;
        }
      }
      tmp_pp.point_num ++;
      tmp_pp.points.push_back(pos_index);
      added_to_region_[pos_index] = true;
      tmp_pp.scatter_matrix = scatter_matrix;
      tmp_pp.sum = sum;
      tmp_pp.mass_center = mass_center;
      tmp_pp.normal = eigenvectors.col(min_eigenvalue_index);
      tmp_pp.bias = tmp_pp.normal.dot(mass_center);
      if (tmp_pp.bias < 0)
      {
        tmp_pp.bias = -tmp_pp.bias;
        tmp_pp.normal = -tmp_pp.normal;
      }
      investigateNeighbors(pos_index);
    }
    }


  planar_patches_->push_back(tmp_pp);

  delete [] added_to_region_;
  delete [] visited_;
  delete [] has_local_plane_;
  delete [] local_mse_;
  delete [] nn_dis_;
  delete [] nn_indices_;
  tree_.reset();

  for (PlanarSegment::StdVector::iterator it = planar_patches_->begin(); it != planar_patches_->end(); it++)
  {
    for (std::vector<int>::iterator sub_it = it->points.begin(); sub_it != it->points.end(); sub_it ++)
    {
      *sub_it = uognzd_indice_to_ognzd_[*sub_it];
    }
  }

  PCL_DEBUG ("%d segments have been identified.\n", planar_patches_->size());
  PCL_DEBUG ("%d points have not been identified to any segment.\n", badpoints_num_);
}

void
OctreeRGSegmentation :: colourAccordingToUncertainty(pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloud)
{
  cloud->points.clear();
  cloud->points.resize(uognzd_cloud_->size());

  PlanarSegment::StdVector planar_patches;
  PlanarSegment::StdVector::iterator it;

  planar_patches.clear();
  for (it = planar_patches_->begin(); it != planar_patches_->end (); it++)
  {
    if (it->area < 0.5)
      continue;
    planar_patches.push_back(*it);
  }

  PlanarSegment tmp;
  for (int i = 0; i < planar_patches.size() - 1; i++)
  {
    for (int j = i + 1; j < planar_patches.size(); j++)
    {
      if (planar_patches[i].Dcovariance > planar_patches[j].Dcovariance)
      {
        tmp = planar_patches[i];
        planar_patches[i] = planar_patches[j];
        planar_patches[j] = tmp;
      }
    }
  }

  planar_patches_->clear();
  planar_patches_->insert(planar_patches_->begin(), planar_patches.begin(), planar_patches.end());

  int cnt = 0;
  for (int i = 0; i < planar_patches.size(); i++)
  {
    double key = static_cast<double >(i) / static_cast<double >(planar_patches.size());
    RGB colour = getHeatColour(key);
    //std::cout << "colour: " << colour.r << "," << colour.g << "," << colour.b << std::endl;
    int32_t rgb_integer = colour.r << 16 | colour.g << 8 | colour.b;
    double rgb = *(double *)(&rgb_integer);
    for (int j = 0; j < planar_patches[i].points.size(); j++)
    {
      int index = planar_patches[i].points[j];
      cloud->points[cnt].x = ognzd_cloud_->points[index].x;
      cloud->points[cnt].y = ognzd_cloud_->points[index].y;
      cloud->points[cnt].z = ognzd_cloud_->points[index].z;
      cloud->points[cnt].rgb = rgb;
      cnt ++;
    }
  }
  cloud->points.erase(cloud->points.begin() + cnt, cloud->points.end());
  cloud->height = 1;
  cloud->width = cnt;
  cloud->resize(cnt);
}

void
OctreeRGSegmentation :: randomColours (pcl::PointCloud<pcl::PointXYZRGB>::Ptr &output,
                                       double min_area,
                                       bool project2plane)
{
  std::vector<RGB> colours;
  getColors(colours);
  output->points.clear();
  output->points.resize(uognzd_cloud_->size());

  srand ( time(NULL) );
  int cnt = 0;
  for (PlanarSegment::StdVector::iterator it = planar_patches_->begin(); it != planar_patches_->end(); it++)
  {
    if (it->area < min_area)
      continue;
    int gray = 255;
    int color_index;
    do{
      color_index = rand() % colours.size();
      gray = (colours[color_index].r + colours[color_index].g + colours[color_index].b) / 3;
    }
    while (gray < 50 || gray > 200);
    int32_t rgb_integer = (colours[color_index].r << 16) | (colours[color_index].g << 8) | colours[color_index].b;
    double rgb = *(double *)(&rgb_integer);
    if (!project2plane)
    {
      for (int j = 0; j < it->points.size(); j++)
      {
        int index = it->points[j];
        output->points[cnt].x = ognzd_cloud_->points[index].x;
        output->points[cnt].y = ognzd_cloud_->points[index].y;
        output->points[cnt].z = ognzd_cloud_->points[index].z;
        output->points[cnt].r = colours[color_index].r;
        output->points[cnt].g = colours[color_index].g;
        output->points[cnt].b = colours[color_index].b;
        //output->points[cnt].rgb = rgb;
        cnt ++;
      }
    }
    else
    {
      for (int j = 0; j < it->points.size(); j++)
      {
        int index = it->points[j];
        double dis = ognzd_cloud_->points[index].x * it->normal(0) +
            ognzd_cloud_->points[index].y * it->normal(1) +
            ognzd_cloud_->points[index].z * it->normal(2) - it->bias;
        output->points[cnt].x = ognzd_cloud_->points[index].x - dis * it->normal(0);
        output->points[cnt].y = ognzd_cloud_->points[index].y - dis * it->normal(1);
        output->points[cnt].z = ognzd_cloud_->points[index].z - dis * it->normal(2);
        output->points[cnt].r = colours[color_index].r;
        output->points[cnt].g = colours[color_index].g;
        output->points[cnt].b = colours[color_index].b;
        //output->points[cnt].rgb = rgb;
        cnt ++;
      }
    }
  }
  output->points.erase(output->points.begin() + cnt, output->points.end());
  output->height = 1;
  output->width = cnt;
  output->resize(cnt);
}


void
OctreeRGSegmentation :: uncertainties()
{
  double weight_sum;
  Eigen::Vector3d weighted_sum, weighted_center;
  /** the weight for each point. */
  std::vector<double > weights;
  /** the scatter matrix. */
  Eigen::Matrix3d S, Hnn, Cnn;
  Eigen::Vector3d Hnd;
  double Hdd, Cdd;
  Eigen::Matrix4d hessian, covariance;
  Eigen::EigenSolver<Eigen::Matrix4d> eigensolver4d;
  Eigen::Vector4d eigenvalues4f = Eigen::Vector4d::Zero();
  Eigen::Matrix4d eigenvectors4d = Eigen::Matrix4d::Zero();
  Eigen::EigenSolver<Eigen::Matrix3d> eigensolver3d;
  Eigen::Vector3d eigenvalues3d = Eigen::Vector3d::Zero();
  Eigen::Matrix3d eigenvectors3d = Eigen::Matrix3d::Zero();
  int min_eigenvalue_index, max_eigenvalue_index;
  double min_eigenvalue, max_eigenvalue;
  PlanarSegment::StdVector::iterator it;
  //std::cout << polynomial_noise[0] << " " << polynomial_noise[1] << " " << polynomial_noise[2] << std::endl;
  for (it = planar_patches_->begin(); it != planar_patches_->end(); it++)
  {
    weights.clear();
    weights.resize(it->points.size());
    weight_sum = 0.0;
    weighted_sum = Eigen::Vector3d::Zero();
    S = Eigen::Matrix3d::Zero ();
    for (int i = 0; i < it->points.size(); i++)
    {
      weights[i] = polynomial_noise[0] +
          polynomial_noise[1] * ognzd_points_[it->points[i]].norm () +
          polynomial_noise[2] * ognzd_points_[it->points[i]].squaredNorm ();
      weights[i] = 1 / (weights[i] * weights[i]);
      weight_sum += weights[i];
      weighted_sum += weights[i] * ognzd_points_[it->points[i]];
    }
    weighted_center = weighted_sum / weight_sum;
    for (int i = 0; i < it->points.size(); i++)
    {
      S += weights[i] * (ognzd_points_[it->points[i]] - weighted_center) * (ognzd_points_[it->points[i]] - weighted_center).transpose ();
    }
    eigensolver3d.compute(S);
    eigenvalues3d = eigensolver3d.eigenvalues().real();
    eigenvectors3d = eigensolver3d.eigenvectors().real();
    min_eigenvalue = eigenvalues3d.minCoeff(&min_eigenvalue_index);

    //    Eigen::Vector3d normal = eigenvectors3d.col(min_eigenvalue_index);
    //    if (normal.dot(weighted_center) < 0)
    //      normal = -normal;
    //    it->normal = normal;
    //    it->bias = normal.dot (weighted_center);

    Hdd = -weight_sum;
    Hnn = -S - weight_sum * weighted_center * weighted_center.transpose() + min_eigenvalue * Eigen::Matrix3d::Identity();
    Hnd = weight_sum * weighted_center;

    hessian.block<3,3>(0,0) = Hnn;
    hessian.block<3,1>(0,3) = Hnd;
    hessian.block<1,3>(3,0) = Hnd.transpose();
    hessian(3,3) = Hdd;

    eigensolver4d.compute(hessian);
    eigenvalues4f = eigensolver4d.eigenvalues().real();
    eigenvectors4d = eigensolver4d.eigenvectors().real();
    max_eigenvalue = eigenvalues4f.cwiseAbs().maxCoeff(&max_eigenvalue_index);
    min_eigenvalue = eigenvalues4f.cwiseAbs().minCoeff(&min_eigenvalue_index);

    //std::cout << "eigenvalues: " << eigenvalues4f.transpose () << std::endl;
    covariance = Eigen::Matrix4d::Zero ();
    double Dcovariance = 1;
    for (int j = 0; j < 4; j++)
    {
      if (j == min_eigenvalue_index)
        continue;
      covariance += eigenvectors4d.col(j) * eigenvectors4d.col(j).transpose () / eigenvalues4f(j);
      Dcovariance /= eigenvalues4f(j);
    }
    covariance = -covariance;
    Dcovariance = -Dcovariance;
    //std::cout << Dcovariance << std::endl;

    double factor = 1.0 / sqrt(1 - eigenvectors4d(3, min_eigenvalue_index) * eigenvectors4d(3, min_eigenvalue_index));
    if (eigenvectors4d(3, min_eigenvalue_index) < 0.0)
      factor = -factor;

    Eigen::Vector3d normal;
    normal(0) = factor * eigenvectors4d(0, min_eigenvalue_index);
    normal(1) = factor * eigenvectors4d(1, min_eigenvalue_index);
    normal(2) = factor * eigenvectors4d(2, min_eigenvalue_index);
    double bias = factor * eigenvectors4d(3, min_eigenvalue_index);
    //std::cout << it->normal.dot(normal) << " " << fabs(bias - it->bias) << std::endl;


    Eigen::Matrix3d Hnn_inv = Hnn.inverse ();
    Eigen::Matrix3d Hnn_prime = Hnn - (1/Hdd) * Hnd * Hnd.transpose();

    eigensolver3d.compute(-Hnn_prime);
    eigenvalues3d = eigensolver3d.eigenvalues().real();
    eigenvectors3d = eigensolver3d.eigenvectors().real();
    min_eigenvalue = eigenvalues3d.cwiseAbs().minCoeff(&min_eigenvalue_index);
    Cnn = Eigen::Matrix3d::Zero ();
    for (int j = 0; j < 3; j++)
    {
      if (j == min_eigenvalue_index)
        continue;
      Cnn += eigenvectors3d.col(j) * eigenvectors3d.col(j).transpose() / eigenvalues3d(j);
    }

    Cdd = -normal.dot(Hnn_inv * normal) / (normal.dot(Hnn_inv * Hnd) * normal.dot(Hnn_inv * Hnd));
    /* write the planar patch corresponding attributes. */
    it->normal = normal;
    it->bias = bias;
    it->hessian = hessian;
    it->Cnn = Cnn;
    it->Cdd = Cdd;
    it->covariance = covariance;
    it->Dcovariance = Dcovariance;
  }
}

void
OctreeRGSegmentation :: segmentsArea()
{
  ///pos_index - width -1     pos_index - 1  pos_index -1 + width
  ///pos_index - width        pos_index      pos_index + width
  ///pos_index + 1 - width    pos_index + 1  pos_index + width + 1
  int height = ognzd_cloud_->height;
  int width = ognzd_cloud_->width;
  std::vector<Eigen::Vector3d, aligned_allocator<Eigen::Vector3d> > cross_products;
  cross_products.resize(planar_patches_->size(), Eigen::Vector3d::Zero());
  int *flags = new int [width * height];
  memset(flags, 9999, width * height * sizeof(int));
  for (PlanarSegment::StdVector::iterator it = planar_patches_->begin(); it != planar_patches_->end(); it++)
  {
    it->area = 0;
    for (vector<int>::iterator sub_it = it->points.begin(); sub_it != it->points.end (); sub_it++)
    {
      flags[*sub_it] = it - planar_patches_->begin();
    }
  }
  int index = 0;
  int flag = -1;
  for (int i = 1; i < height - 1; i++)
  {
    for (int j = 1; j < width - 1; j++)
    {
      index = i * width + j;
      flag = flags[index];
      if (flag < 9999)
      {
        int key = (flags[index + width] == flag) * 4 +
            (flags[index + width + 1] == flag) * 2 +
            (flags[index + 1] == flag);
        switch (key)
        {
          case 7:
            cross_products[flag] += ognzd_points_[index].cross(ognzd_points_[index + width]) +
                ognzd_points_[index + width].cross(ognzd_points_[index + width + 1]) +
                ognzd_points_[index + width + 1].cross(ognzd_points_[index + 1]) +
                ognzd_points_[index + 1].cross(ognzd_points_[index]);//));
            break;
          case 3:
            cross_products[flag] += ognzd_points_[index].cross(ognzd_points_[index + width + 1]) +
                ognzd_points_[index + width + 1].cross(ognzd_points_[index + 1]) +
                ognzd_points_[index + 1].cross(ognzd_points_[index]);//));
            break;
          case 5:
            cross_products[flag] += ognzd_points_[index].cross(ognzd_points_[index + width]) +
                ognzd_points_[index + width].cross(ognzd_points_[index + 1]) +
                ognzd_points_[index + 1].cross(ognzd_points_[index]);//));
            break;
          case 6:
            cross_products[flag] += ognzd_points_[index].cross(ognzd_points_[index + width]) +
                ognzd_points_[index + width].cross(ognzd_points_[index + width + 1]) +
                ognzd_points_[index + width + 1].cross(ognzd_points_[index]);//));
            break;
          default:
            break;
        }
        if ((flags[index - width - 1] != flag) && (flags[index - 1] == flag) && (flags[index - width] == flag))
        {
          cross_products[flag] += ognzd_points_[index].cross(ognzd_points_[index - width]) +
              ognzd_points_[index - width].cross(ognzd_points_[index - 1]) +
              ognzd_points_[index - 1].cross(ognzd_points_[index]);//));
        }
      }
    }
  }
  for (PlanarSegment::StdVector::iterator it = planar_patches_->begin (); it != planar_patches_->end(); it++)
  {
    it->area = 0.5 * fabs(it->normal.dot(cross_products[it - planar_patches_->begin ()]));
  }
  delete [] flags;
}
