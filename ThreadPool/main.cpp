#include <ctime>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include "threadpool.hpp"
using namespace gk;

typedef unsigned char uchar;

class Mat
{
public:
	uchar* data;
	int rows, cols;

	Mat(int r, int c)
	{
		if (r * c > 0)
		{
			data = static_cast<uchar*>(malloc(r * c * sizeof(data[0])));
			assert(data);
			rows = r;
			cols = c;
		}
		else
		{
			data = nullptr;
			rows = cols = 0;
		}
	}

	~Mat()
	{
		if (data)
			free(data);
	}
};


class Mandelbrot : public gk::TPLoopBody
{
	enum { Iteration = 300 };
	Mat& m;
	int rows, cols;
	double x0, y0, ppi;

public:
	Mandelbrot(Mat& mat, double ox, double oy, double radius)
		: m(mat), rows(m.rows), cols(m.cols)
	{
		x0 = ox - radius;
		y0 = oy - radius;
		ppi = 2 * radius / std::min(rows, cols);
	}

	// per-row
	void operator ()(Range const& range) const override
	{
		for (int h = range.start; h < range.end; ++h)
		{
			uchar* M = m.data + h * cols;
			double Y = y0 + h * ppi;
			for (int w = 0; w < cols; ++w)
			{
				double X = x0 + w * ppi;
				double x = 0, y = 0, t;
				double z = x * x + y * y;
				int iter = 0;
				while (z < 4 && iter < Iteration)
				{
					++iter;
					t = x * x - y * y + X;
					y = 2 * x * y + Y;
					x = t;
					z = x * x + y * y;
				}
				// make gradient smother
				if (z > 4)
					z = iter - log2(log2(z) * 0.5);
				else
					z = iter;
				z *= 255.0 / Iteration;
				M[w] = static_cast<uchar>(z);
			}
		}
	}
};


bool pgm_write(Mat const& img, char const* name)
{
	size_t size = sizeof(uchar) * img.rows * img.cols;
	std::ofstream ofs;
	ofs.open(name, std::ios::binary | std::ios::trunc);
	if (!(ofs.is_open()))
		return false;
	ofs << "P5\n" << img.cols << " " << img.rows << "\n255\n";
	ofs.write(reinterpret_cast<char const*>(img.data), size);
	return static_cast<size_t>(ofs.tellp()) == size;
}


void draw(double ox, double oy, double radius, int size)
{
	Mat img(size, size);
	char buf[1 << 10];
	parallel_for(Range(0, size), Mandelbrot(img, ox, oy, radius), true);
	sprintf(buf, "G:/Sample/mandelbrot_%f.pgm", radius);
	pgm_write(img, buf);
}


int main()
{
	int nums[6] = { 0, 1, 2, 5, 3, 6 };
	for (size_t i = 0; i < 6; ++i)
		set_num_thread(nums[i]);

	clock_t t0 = clock();
	int size = 1000;
	double x = 0.27322626, y = 0.595153338;
	draw(-0.75, 0, 1.5, size);
	for (int i = 2; i < 7; ++i)
		draw(x, y, pow(0.2, i - 1), size);
	clock_t t1 = clock();

	printf("Hello, World! %d, %f ms\n",
		get_num_thread(), 1e3 * (t1 - t0) / CLOCKS_PER_SEC);
	return 0;
}


