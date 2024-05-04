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

inline int rand_int(int a, int b) { return rand() % (b - a + 1) + a; }

float rand_float(float a, float b) {
	float t = ((float) rand())/((float)RAND_MAX);
	return a + t * (b - a);
}

typedef struct {
	int i;
	int j;
} Point;

#define POINT_QUEUE_MAX 10000

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

inline bool point_queue_empty(PointQueue *q) { return q->size == 0; }

Point point_from_coordinates(int i, int j) {
	Point point;
	point.i = i;
	point.j = j;
	return point;
}

Point random_point(int width, int height) { 
	return point_from_coordinates(
			rand_int(0, height - 1), 
			rand_int(0, width - 1)
	); 
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

Circle random_circle(int width, int height, float r) {
	return circle_from(rand_int(0, height - 1), rand_int(0, width - 1), r);
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


void image_free(Image *image) { stbi_image_free(image->data); }

float sq(float x) { return x * x; }

float distance(Point fst, Point snd) { return sqrtf(sq(fst.i - snd.i) + sq(fst.j - snd.j)); }

void copy_px(Image *to_img, Image *from_img, Point to, Point from) {
	int i = (to.i * to_img->width + to.j) * to_img->channels;
	int j = (from.i * from_img->width + from.j) * from_img->channels;

	for (int k = 0; k < to_img->channels; k++) {
		to_img->data[i + k] = from_img->data[j + k];	
	}
}

void expand(Image *result, Image *original, bool *can_use, Circle circle) {
	static PointQueue q;
	static int directions[8][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
	point_queue_reset(&q);
	
	point_queue_add(&q, circle.point);

	while (!point_queue_empty(&q)) {
		Point top = point_queue_pop(&q);
		copy_px(result, original, top, circle.point);
		can_use[(top.i * result->width) + top.j] = false;

		for (int k = 0; k < 8; k++) {
			Point candidate;
			int i = top.i + directions[k][0];
			int j = top.j + directions[k][1];
			candidate.i = i;
			candidate.j = j;

			if (can_use[i * result->width + j] 
					&& 0 <= i && i < result->height && 0 <= j && j < result->width 
					&& distance(circle.point, candidate) <= circle.radius) {
				can_use[i * result->width + j] = false;
				point_queue_add(&q, candidate);
			}
		}
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

Point line_get(const Line* line, float t) {
	return point_from_coordinates(line->origin.i + ((float) line->direction.i) * t, line->origin.j + ((float) line->direction.j) * t);
}

bool any_in_sample(bool *can_use, int width, int height, const Line *line, float radius, int depth) {
	static int max_depth = 12;

	if (depth > max_depth) {
		return false;
	}

	Point ray = line_get(line, radius / 2);

	int i_0 = ray.i;
	int j_0 = ray.j;
	if (i_0 < 0 || j_0 < 0 || i_0 >= height || j_0 >= width) {
		return any_in_sample(can_use, width, height, line, radius * 0.25, depth + 1);
	}
	
	if (!can_use[i_0 * width + j_0]) {
		return true;
	}

	return any_in_sample(can_use, width, height, line, radius * 0.25, depth + 1) || 
		any_in_sample(can_use, width, height, line, 0.75 * radius, depth + 1);
}

bool any_in_near_perimeter(bool *can_use, Point origin, int width, int height, float radius) {
	float delta = M_PI / 12;
	Pointf originf = pointf_from_coordinates(origin.i, origin.j);
	for (float theta = 0; theta < 2 * M_PI; theta += delta) {
		Line line = line_from(originf, pointf_from_coordinates(sin(theta), cos(theta)));
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
	bool can_use[result.width * result.height];
	memset(can_use, true, result.width * result.height * sizeof(bool));

	/*
	for (int i = 0; i < result.height; i++) {
		for (int j = 0; j < result.width; j++) {
			int index = (i * result.width + j) * result.channels;
			for (int k = 0; k < result.channels; k++) {
				result.data[index + k] = 0;
			}
			if (result.channels == 4) {
				result.data[index + 3] = 255;
			}
		}
	}
	*/

	int n = 0;
	float min_radius = 2;
	float max_radius = 200;
	float circle_likelihood = 0.60;

	for (int i = 0; i < result.height; i++) {
		for (int j = 0; j < result.width; j++) {
			if (rand_float(0, 1) > circle_likelihood || !can_use[i * result.width + j]) {
				continue;
			}
			
			Point p = point_from_coordinates(i, j);
			float radius = rand_float(min_radius, max_radius);
			if (any_in_near_perimeter(can_use, p, result.width, result.height, radius)) {
				continue;
			}

			Circle circle = circle_from(i, j, radius);
			expand(&result, &original, can_use, circle);
		}
	}
	
	stbi_write_png("result.png", result.width, result.height, result.channels, result.data, result.width * result.channels);
	printf("Finished\n");	

	image_free(&original);
	free(result.data);
	return 0;
}

