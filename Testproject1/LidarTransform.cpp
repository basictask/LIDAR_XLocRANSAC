#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <iostream>
#include <fstream>
#include <math.h> 

using namespace cv;
using namespace std;

#define sampleSize 4 // How many elements to pick out
#define THRESHOLD 0.05 // RANSAC threshold
#define ITER 500 // Ransac iteration number
#define OBJS 3 // Number of planes to detect

vector<float> splitString(string arg, char splitter) // Split a string on given character and return as an array
{
	stringstream test(arg);
	string segment;
	vector<float> seglist;

	while (std::getline(test, segment, splitter))
	{
		if (segment != "")
		{
			seglist.push_back(std::stof(segment));
		}
	}
	return seglist;
}

vector<pair<Point3f, int>> readFile(char** argv, bool header) // Read points into a vector
{
	vector<pair<Point3f, int>> pairs;
	fstream newfile;
	newfile.open(argv[1], ios::in);

	if (newfile.is_open())
	{
		string tp;
		while (getline(newfile, tp))
		{
			if (header)
			{
				header = false;
			}
			else
			{
				vector<float> data = splitString(tp, ','); // Split by character
				if (data.size() == 10)
				{
					pair<Point3f, int> elem;
					elem.first = Point3f((float)data[7], (float)data[8], (float)data[9]);
					elem.second = (int)data[0];
					pairs.push_back(elem);
				}
			}
		}
		newfile.close();
	}
	else
	{
		cout << "Cannot open file.txt" << endl;
		return pairs;
	}
	return pairs;
}

vector<pair<Point3f, int>> removeElements(vector<pair<Point3f,int>> set, vector<bool> mask, string mode)
{
	vector<pair<Point3f, int>> result;
	for (int i = 0; i < mask.size(); i++) {

		if (mask.at(i)) // True --> inlier
		{
			if (mode == "inlier")
			{
				result.push_back(set.at(i));
			}
		}
		else // False --> outlier
		{
			if (mode == "outlier") // Only add if we want to remove outliers
			{
				result.push_back(set.at(i));
			}
		}
	}
	return result;
}

vector<bool> getEmptyMask(int size) // Creates an empty inlier/outlier mask full of false elements
{
	vector<bool> result;
	for (int i = 0; i < size; i++)
	{
		result.push_back(false);
	}
	return result;
}

vector<Point3f> getFirst(vector<pair<Point3f, int>> points) // Retrieve points from the pairs
{
	vector<Point3f> result;
	for (int i = 0; i < points.size(); i++)
	{
		result.push_back(points.at(i).first);
	}
	return result;
}

Point3f centerOfGravity(vector<Point3f> pts) // Calculate the center of gravity given a set of points in 3D
{
	Point3f cog;
	int num = pts.size();

	for (int i = 0; i < num; i++)
	{
		cog.x += pts.at(i).x;
		cog.y += pts.at(i).y;
		cog.z += pts.at(i).z;
	}

	cog.x /= num;
	cog.y /= num;
	cog.z /= num;

	return cog;
}

float* estimatePlane(vector<Point3f> pts) // Fit a plane to a subset of points
{
	int num = pts.size();
	Point3f cog = centerOfGravity(pts);
	Mat Q(num, 4, CV_32F);

	for (int idx = 0; idx < num; idx++) // Form a linear set of equations
	{
		Point3d pt = pts.at(idx);
		Q.at<float>(idx, 0) = pt.x - cog.x;
		Q.at<float>(idx, 1) = pt.y - cog.y;
		Q.at<float>(idx, 2) = pt.z - cog.z;
		Q.at<float>(idx, 3) = 1;
	}

	Mat mtx = Q.t() * Q;

	Mat evals, evecs;
	eigen(mtx, evals, evecs);

	float A = evecs.at<float>(3, 0);
	float B = evecs.at<float>(3, 1);
	float C = evecs.at<float>(3, 2);
	float D = (-A * cog.x - B * cog.y - C * cog.z);

	float norm = sqrt(A*A + B * B + C * C);

	float* ret = new float[4];

	ret[0] = A / norm;
	ret[1] = B / norm;
	ret[2] = C / norm;
	ret[3] = D / norm;

	return ret;
}

vector<Point3f> pickRandomElements(int n, vector<pair<Point3f, int>> points) // Generate n different random integers, in [0, num] range
{
	int num = points.size();
	vector<Point3f> result_arr;
	vector<int> result = vector<int>();

	while (result.size() < n)
	{
		float randf = (float)(rand()) / RAND_MAX;
		int randi = (int)(randf * num);
		if (result.size() == 0)
		{
			result.push_back(randi);
		}
		else
		{
			bool flag = false;
			for (int i = 0; i < result.size(); i++)
			{
				if (result.at(i) == randi)
				{
					flag = true;
				}
			}
			if (!flag)
			{
				result.push_back(randi);
			}
		}
	}
	
	for (int i = 0; i < n; i++)
	{
		result_arr.push_back(points.at(result.at(i)).first);
	}

	return result_arr;
}

vector<float> calculateDistances(vector<pair<Point3f, int>> points, float* params)
{
	vector<float> result;
	for (int i = 0; i < points.size(); i++)
	{
		Point3f pt = points.at(i).first;
		result.push_back(fabs(params[0] * pt.x + params[1] * pt.y + params[2] * pt.z + params[3]));
	}
	return result;
}

int flagInliers(vector<bool> &mask, vector<float> distances) // Flag true on mask wherever an inlier is found according to distance vector
{
	int inlierCount = 0;
	for (int i = 0; i < distances.size(); i++) 
	{
		if (distances.at(i) < THRESHOLD) // Inlier
		{
			inlierCount++;
			if (!mask.at(i)) // Has not been flagged as an inlier before
			{
				mask.at(i) = true;
			}
		}
	}
	return inlierCount;
}

void outputCloud(vector<pair<Point3f, int>> points, int i, string message) // Output the points into a pointCloud
{
	// Get name of file 
	char filename[10];
	std::string s = std::to_string(i);
	char const *pchar = s.c_str();
	strcpy_s(filename, "o");
	strcat_s(filename, "u");
	strcat_s(filename, "t");
	strcat_s(filename, pchar);
	strcat_s(filename, ".");
	strcat_s(filename, "x");
	strcat_s(filename, "y");
	strcat_s(filename, "z");

	// Output file into a file Outx.xyz x=(0,1,..,num)
	int num = points.size();
	ofstream myfile;
	myfile.open(filename);
	myfile << message << endl;

	for (int i = 0; i < num; i++) // Iterate over the points and append each coordinate to the file
	{
		Point3f currpt = points.at(i).first;
		myfile << currpt.x << " " << currpt.y << " " << currpt.z << " " << endl;
	}

	myfile << endl;
	myfile.close();
}

int main(int argc, char** argv)
{
	vector<pair<Point3f, int>> points = readFile(argv, true);
	vector<pair<Point3f, int>> subset = points;
	int num = points.size();
	vector<bool> fullMask = getEmptyMask(num);

	cout << "Starting robust estimation on " << num << " points."<< endl << endl;

	// Number of planes to detect
	for (int h = 0; h < OBJS; h++) // Iterate over objects
	{	
		int bestInlierNum = 0;
		float* bestParams = new float[4];
		vector<bool> bestInlierSubMask;

		for (int i = 0; i < ITER; i++) // RANSAC iteration
		{
			vector<Point3f> ranSample = pickRandomElements(sampleSize, subset); // Randomly pick 4 elements from the subset
			
			float* params = estimatePlane(ranSample); // Fit a plane to the 4 randomly picked points
			
			vector<float> distances = calculateDistances(subset, params); // Calculate distances for inlier thresholding
			
			vector<bool> subMask = getEmptyMask(subset.size()); // Create an inlier -> true / outlier -> false mask --> all false now
			
			int inlierCount = flagInliers(subMask, distances); // Set inliers from false to true

			if (inlierCount > bestInlierNum) // Assign new best variables if necessary
			{
				bestInlierNum = inlierCount;
				bestInlierSubMask = subMask;
				bestParams = params;
			}
		}

		vector<Point3f> inlierPoints = getFirst(removeElements(points, bestInlierSubMask, "inlier")); // Get point set for inlier points
		
		float* inlierParams = estimatePlane(inlierPoints); // Reestimate the plane on the inlier set

		vector<float> distancesFull = calculateDistances(points, inlierParams); // Calculate distances for reestimated plane

		vector<bool> refMask = getEmptyMask(num); // Create an empty mask for newly selected points

		int inlierCountFull = flagInliers(fullMask, distancesFull); // Flag inliers for all iterations

		int inlierCountRef = flagInliers(refMask, distancesFull); // Flag inliers for current iteration 

		subset = removeElements(points, fullMask, "outlier"); // Keep only outliers
		
		vector<pair<Point3f, int>>  refPoints = removeElements(points, refMask, "inlier"); // Keep only inliers
			 
		outputCloud(subset, h, "Created by Daniel Kuknyo"); // Output the full outlier pointcloud 

		outputCloud(refPoints, h + 5, "Reference points for specific iteration"); // Output the reference point cloud

		cout << "Plane params: " << inlierParams[0] << ", " << inlierParams[1] << ", " << inlierParams[2] << ", " << inlierParams[3] << endl;
		cout << "Number of total inliers: " << inlierCountFull << endl;
		cout << "Finished processing plane " << h << endl << endl;
	}

	return 0;
}