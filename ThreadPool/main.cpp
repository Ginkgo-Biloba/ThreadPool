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
	enum { Iteration = 1000 };
	Mat& m;
	double ppi;
public:
	Mandelbrot(Mat& mat) : m(mat)
	{
		if (m.rows * m.cols > 0)
			ppi = 4.0 / std::min(m.rows, m.cols);
		else
			ppi = 0;
	}

	// 逐像素？逐行？
	void operator ()(Range const& range) const override
	{
		int hrow = m.rows / 2;
		int hcol = m.cols * 2 / 3;
		for (int h = range.start; h < range.end; ++h)
		{
			double Y = (h - hrow) * ppi;
			for (int w = 0; w < m.cols; ++w)
			{
				double X = (w - hcol) * ppi;
				double x = 0, y = 0, z = 0;
				int iter = 0, val = 0;
				do
				{
					z = x * x - y * y + X;
					y = 2 * x * y + Y;
					x = z;
					z = x * x + y * y;
					++iter;
				} while (z < 4 && iter < Iteration);
				if (iter < Iteration)
				{
					x = sqrt(iter / static_cast<double>(Iteration));
					val = static_cast<int>(x * 255 + 0.5);
				}
				m.data[h * m.cols + w] = static_cast<uchar>(val);
			}
		}
	}
};


bool savePGM(Mat const& img, char const* name)
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


int main()
{
	Mat m(3, 400), n(2000, 2000);
	Mandelbrot m_par(m), n_par(n);
	int method = 2;
	//set_num_thread(4);

	clock_t t0 = clock();
	if (method == 0)
	{
		m_par(Range(0, m.rows));
		n_par(Range(0, n.rows));
	}
	else if (method == 1)
	{
		parallel_for(Range(0, n.rows), n_par);
		parallel_for(Range(0, m.rows), m_par);
	}
	else
	{
		// 先多后少
		parallel_for(Range(0, m.rows), m_par);
		parallel_for(Range(0, n.rows), n_par);
	}
	clock_t t1 = clock();

	savePGM(m, "G:\\Sample\\mandelbrot0.pgm");
	savePGM(n, "G:\\Sample\\mandelbrot1.pgm");
	printf("Hello, World! %d, %f ms\n",
		get_num_thread(), 1e3 * (t1 - t0) / CLOCKS_PER_SEC);
	return 0;
}


