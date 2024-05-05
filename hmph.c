#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int rand_int(int a, int b) { return rand() % (b - a + 1) + a; }
// returns a float in the interval [0, 1]
float rand_small_float() { return ((float) rand())/((float)RAND_MAX); }
float lerp(float a, float b, float t) { return t * (b - a) + a; }
float rand_float(float a, float b) { return lerp(a, b, rand_small_float()); }

typedef struct {
	int i;
	int j;
} Point;

#define POINT_QUEUE_MAX 8192

typedef struct {
	Point points[POINT_QUEUE_MAX];
	int top;
	int start;
	int size;
} PointQueue;

void point_queue_reset(PointQueue *q) {
	q->top = 0;
	q->start = 0;
	q->size = 0;
}

Point point_queue_pop(PointQueue *q) {
	Point result = q->points[q->start];
	q->start = (q->start + 1) % POINT_QUEUE_MAX;
	q->size--;
	return result;
}

void point_queue_add(PointQueue *q, Point point) {
	if (q->size + 1 > POINT_QUEUE_MAX) {
		fprintf(stderr, "Need more space\n");
		exit(1);
	}
	int next_top = (q->top + 1) % POINT_QUEUE_MAX;
	q->points[q->top] = point;
	q->top = next_top;
	q->size++;
}

bool point_queue_empty(PointQueue *q) { return q->size == 0; }

Point point_from_coordinates(int i, int j) {
	Point point;
	point.i = i;
	point.j = j;
	return point;
}

typedef struct {
	Point point;
	float radius;
} Circle;

Circle circle_from(int i, int j, float r) {
	Circle circle;
	circle.point = point_from_coordinates(i, j);
	circle.radius = r;
	return circle;
}

typedef struct {
	int width;
	int height;
	int channels;
	unsigned char* data;
} Image;

Image image_load_from_path(const char *path) {
	int width;
	int height;
	int channels;
	unsigned char* data = stbi_load(path, &width, &height, &channels, 0);
	if (data == NULL) {
		fprintf(stderr, "Failed loading file\n");
		exit(1);
	}

	Image image;
	image.width = width;
	image.height = height;
	image.channels = channels;
	image.data = data;
	return image;
}

Image image_clone(const Image *image) {
	Image result;
	result.width = image->width;
	result.height = image->height;
	result.channels = image->channels;
	size_t n = image->width * image->height * image->channels;
	result.data = malloc(n);
	memcpy(result.data, image->data, n);
	return result;
}

void image_set_blank(Image *image) {
	for (int i = 0; i < image->height; i++) {
		for (int j = 0; j < image->width; j++) {
			int index = (i * image->width + j) * image->channels;
			for (int k = 0; k < image->channels; k++) {
				image->data[index + k] = 0;
			}
			if (image->channels == 4) {
				image->data[index + 3] = 255;
			}
		}
	}
}


void image_free(Image *image) { stbi_image_free(image->data); }

float sq(float x) { return x * x; }
float euclid_distance(Point fst, Point snd) { return sqrtf(sq(fst.i - snd.i) + sq(fst.j - snd.j)); }
int manhat_distance(Point fst, Point snd) { return abs(fst.i - snd.i) + abs(fst.j - snd.j); }

void copy_px(Image *to_img, Image *from_img, Point to, Point from) {
	int i = (to.i * to_img->width + to.j) * to_img->channels;
	int j = (from.i * from_img->width + from.j) * from_img->channels;

	for (int k = 0; k < to_img->channels; k++) {
		to_img->data[i + k] = from_img->data[j + k];	
	}
}

typedef struct {
	float i;
	float j;
} Pointf;

Pointf pointf_from_coordinates(float i, float j) {
	Pointf result;
	result.i = i;
	result.j = j;
	return result;
}

Point point_from_pointf(Pointf pt) {
	Point result;
	result.i = pt.i;
	result.j = pt.j;
	return result;
}

typedef struct {
	Pointf origin;
	Pointf direction;
} Line;

Line line_from(Pointf origin, Pointf direction) {
	Line result;
	result.origin = origin;
	result.direction = direction;
	return result;
}

Pointf line_get(const Line* line, float t) {
	return pointf_from_coordinates(line->origin.i + line->direction.i * t, line->origin.j + line->direction.j * t);
}

void expand(Image *result, Image *original, bool *can_use, Circle circle) {
	static PointQueue q;
	static int directions[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
	point_queue_reset(&q);
	
	point_queue_add(&q, circle.point);
	can_use[circle.point.i * result->width + circle.point.j] = false;


	float delta = 0.0001;
	Pointf originf = pointf_from_coordinates(circle.point.i, circle.point.j);
	for (float theta = 0; theta < 2 * M_PI; theta += delta) {
		Line line = line_from(originf, pointf_from_coordinates(sin(theta), cos(theta)));
		Point perim = point_from_pointf(line_get(&line, circle.radius));
		int i = perim.i;
		int j = perim.j;
		if (i >= 0 && j >= 0 && i < result->height && j < result->width) {
			copy_px(result, original, perim, circle.point);
			can_use[i * result->width + j] = false;
		}
	}
	
	while (!point_queue_empty(&q)) {
		Point top = point_queue_pop(&q);
		copy_px(result, original, top, circle.point);

		for (int k = 0; k < 4; k++) {
			Point candidate;
			int i = top.i + directions[k][0];
			int j = top.j + directions[k][1];
			candidate.i = i;
			candidate.j = j;

			if (0 <= i && i < result->height && 0 <= j && j < result->width
				&& can_use[i * result->width + j]) {
				can_use[i * result->width + j] = false;
				point_queue_add(&q, candidate);
			}
		}
	}
}

bool any_in_sample(const bool *can_use, int width, int height, const Line *root_line, float radius, int depth) {
	float delta = 1.0 / 8.0;
	
	// move percentages of the radius along the parameterized line
	for (float t = 0; t <= 1; t += delta) {
		Point partial_segment_end = point_from_pointf(line_get(root_line, t * radius));
		int i = partial_segment_end.i;
		int j = partial_segment_end.j;
		if (i < 0 || j < 0 || i >= height || j >= width) {
			break;
		}

		if (!can_use[i * width + j]) {
			return true;
		}
	}

	return false;
}

bool any_in_near_perimeter(const bool *can_use, Point origin, int width, int height, float radius) {
	float delta = 0.001;
	Pointf originf = pointf_from_coordinates(origin.i, origin.j);
	if (!can_use[origin.i * width + origin.j]) {
		return true;
	}

	for (float theta = 0; theta < 2 * M_PI; theta += delta) {
		Line line = line_from(originf, pointf_from_coordinates(sin(theta), cos(theta)));
//		Point p = point_from_pointf(line_get(&line, radius));
//		if (0 <= p.i && p.i < height && 0 <= p.j && p.j < width && !can_use[p.i * width + p.j]) {
//			return true;
//		}
		if (any_in_sample(can_use, width, height, &line, radius, 0))  {
			return true;
		}
	}

	return false;
}

int main(int argc, char **argv) {
	srand(time(NULL));
	if (argc < 2) {
		fprintf(stderr, "Missing path to image\n");
		return 1;
	}

	Image original = image_load_from_path(argv[1]);
	Image result = image_clone(&original);
	bool *can_use = malloc(result.width * result.height * sizeof(bool));
	memset(can_use, true, result.width * result.height * sizeof(bool));
	image_set_blank(&result);

	float min_radius = 2;
	float max_radius = 1000;
	float circle_likelihood = 0.02;
	float partition_factor = 3;

	for (int i = 0; i < result.height; i++) {
		for (int j = 0; j < result.width; j++) {
			if (rand_small_float() > circle_likelihood || !can_use[i * result.width + j]) {
				continue;
			}
			
			Point origin = point_from_coordinates(i, j);
			float radius = rand_float(min_radius, max_radius);

			while (radius >= min_radius && any_in_near_perimeter(can_use, origin, result.width, result.height, radius)) {
				radius /= partition_factor;
			}

			if (radius < min_radius) {
				continue;
			}

			Circle circle = circle_from(i, j, radius);
			expand(&result, &original, can_use, circle);
		}
	}
	
	stbi_write_png("result_.png", result.width, result.height, result.channels, result.data, result.width * result.channels);
	printf("Finished\n");	

	image_free(&original);
	free(result.data);
	free(can_use);
	return 0;
}

