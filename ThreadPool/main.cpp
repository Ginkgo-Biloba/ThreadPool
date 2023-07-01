#include <ctime>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <vector>
#include "asyncpool.hpp"
#include "syncpool.hpp"
using namespace gk;
using std::vector;

typedef unsigned char uchar;

static int saveppm = 0;

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
			log_assert(data);
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
		data = nullptr;
		rows = cols = -1;
	}
};

static void draw_mandelbrot(Mat& m, double x0, double y0, double ppi, int start, int stop)
{
	int const iteration = 300;
	for (int h = start; h < stop; ++h)
	{
		uchar* M = m.data + h * m.cols;
		double Y0 = y0 + h * ppi;
		for (int w = 0; w < m.cols; ++w)
		{
			double X0 = x0 + w * ppi;
			double x = 0, y = 0, t;
			double z = x * x + y * y;
			int iter = 0;
			while (z < 4 && iter < iteration)
			{
				++iter;
				t = x * x - y * y + X0;
				y = 2 * x * y + Y0;
				x = t;
				z = x * x + y * y;
			}
			// make gradient smother
			if (z > 4)
				z = iter - log2(log2(z) * 0.5);
			else
				z = iter;
			z *= 255.0 / iteration;
			M[w] = static_cast<uchar>(z);
		}
	}
}

class Mandelbrot : public SyncJob
{
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
	void call(int from, int to) override
	{
		draw_mandelbrot(m, x0, y0, ppi, from, to);
	}
};

bool ppm_write(Mat const& img, char const* name)
{
	size_t size = sizeof(uchar) * img.rows * img.cols;
	std::ofstream ofs;
	ofs.open(name, std::ios::binary | std::ios::trunc);
	if (!(ofs.is_open()))
		return false;
	ofs << "P5\n"
			<< img.cols << " " << img.rows << "\n255\n";
	ofs.write(reinterpret_cast<char const*>(img.data), size);
	return static_cast<size_t>(ofs.tellp()) == size;
}

void draw(double ox, double oy, double radius, int size, SyncPool& pool)
{
	Mat img(size, size);
	char buf[128];
	RefPtr<SyncJob> m = new Mandelbrot(img, ox, oy, radius);
	m->start = 0;
	m->stop = size;
	pool.submit(m);
	sprintf(buf, "mandelbrot_%f.ppm", radius);
	if (saveppm)
		ppm_write(img, buf);
}

class MandelAsync : public AsyncTask
{
	Mat m;
	int rows, cols;
	double rad, x0, y0, ppi;

public:
	MandelAsync(int size, double ox, double oy, double radius)
		: m(size, size), rows(m.rows), cols(m.cols), rad(radius)
	{
		x0 = ox - radius;
		y0 = oy - radius;
		ppi = 2 * radius / std::min(rows, cols);
	}

	void call() override
	{
		char buf[128];
		draw_mandelbrot(m, x0, y0, ppi, 0, m.rows);
		sprintf(buf, "mandelbrot_%f.ppm", rad);
		if (saveppm)
			ppm_write(m, buf);
	}
};

int main()
{
	saveppm = 0;
	puts("├Hello, World┤");
	double sum_tick = clock();
	int nums[6] = {0, 4, 2, 5, 3, 6};
	int size = 2000;
	double x = 0.27322626, y = 0.595153338;
	if (1)
	{
		SyncPool pool;
		for (size_t i = 0; i < 6; ++i)
			pool.set(nums[i]);
		draw(-0.75, 0, 1.5, size, pool);
		for (int i = 2; i < 7; ++i)
			draw(x, y, pow(0.2, i - 1), size, pool);
	}
	else
	{
		AsyncPool pool;
		vector<RefPtr<AsyncTask>> tasks;
		for (size_t i = 0; i < 6; ++i)
			pool.set(nums[i]);
		RefPtr<AsyncTask> t1 = new MandelAsync(size, -0.75, 0, 1.5);
		pool.submit(t1);
		for (int i = 2; i < 7; ++i)
			tasks.push_back(new MandelAsync(size, x, y, pow(0.2, i - 1)));
		pool.submit(tasks.data(), tasks.size());
		tasks.clear();
		t1->wait();
		pool.wait();
	}
	sum_tick = clock() - sum_tick;
	printf("main: %f ms\n", sum_tick * 1e3 / CLOCKS_PER_SEC);
}
