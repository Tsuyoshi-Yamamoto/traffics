
#include <cv.h>
#include <highgui.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "RaspiCamCV.h"

#define RASPBERRY_PI

//#define VERBOSE
#define N_FRAMES 480
int n_frames =  N_FRAMES;
IplImage* frames[1200];

#ifdef RASPBERRY_PI

#define QUERY_FRAME(cam) raspiCamCvQueryFrame(cam)
#define RELEASE_CAPTURE(cam) raspiCamCvReleaseCapture(cam)

RaspiCamCvCapture *cam = NULL;

bool setup_camera(){
	RASPIVID_CONFIG * config = (RASPIVID_CONFIG*)malloc(sizeof(RASPIVID_CONFIG));
	config->width=1280;
	config->height=720;
	config->bitrate=0;	// zero: leave as default
	config->framerate=120;
	config->monochrome=1;
	cam = (RaspiCamCvCapture *)raspiCamCvCreateCameraCapture2(0, config);
	free(config);
	if(cam == NULL){
		printf("Can't open picamera\n");
		return false;
	}
	return true;
}
#else

#define QUERY_FRAME(cam) cvQueryFrame(cam)
#define RELEASE_CAPTURE(cam) cvReleaseCapture(cam)

CvCapture *cam = NULL;

bool setup_camera()
{
	if((cam = cvCaptureFromCAM(0)) == NULL){
		printf("Can't open default camera\n");
		return false;
	};
//	double fps=cvGetCaptureProperty(cam,CV_CAP_PROP_FPS);
//	printf("Camera default FPS=%f\n",fps);
	return true;
}
#endif

void init_frames(IplImage *base)
{
#ifdef VERBOSE
	printf("Frame buffer allocation, size=%d MB\n",
					(base->imageSize*n_frames)/(1024*1023));
#endif

//	IplImage scratch = cvCloneImage(base);	// scratch pad
	for(int i=0;i<n_frames;i++){
		frames[i]=cvCloneImage(base);
	}
}

//	average intensity of roi
double average(IplImage *img, cv::Rect roi)
{
	double sum=0.0;
	int pv;
	for(int y=roi.y; y<roi.y+roi.height;y++)
		for(int x=roi.x; x<roi.x+roi.width;x++){
			pv = (255 & img->imageData[img->widthStep*y+x]);
			sum += pv;
		};
	return sum/(roi.width*roi.height);
}

double diffsum(IplImage *base, IplImage *next, cv::Rect roi)
{
	double dsum=0.0;
	int p1,p2;
	for(int y=roi.y; y<roi.y+roi.height;y++)
		for(int x=roi.x; x<roi.x+roi.width;x++){
			p1 = (255 & base->imageData[base->widthStep*y+x]);
			p2 = (255 & next->imageData[next->widthStep*y+x]);
			dsum += abs(p1-p2);
		};
#ifdef VERBOSE
	printf("roi=(%d,%d,%d,%d)\n",roi.x, roi.y, roi.width, roi.height);
	printf("image difference= %f\n",
			dsum/(roi.width*roi.height));
	printf("at(%d,%d) p1=%d, p2=%d\n",roi.x-1,roi.y-1,p1,p2);
#endif
	return dsum/(roi.width * roi.height);
}

void describe_IplImage(IplImage *src)
{
	printf("dim=%d x %d,nch=%d,depth=%d, imageSize=%d, widthStep=%d\n",
		src->width, src->height, src->nChannels,
		src->depth, src->imageSize, src->widthStep);
}

void describe_Mat(cv::Mat mat)
{
	printf("dim=%d x %d, nCh=%d, depth=%d",
		mat.cols, mat.rows, mat.channels(),mat.depth());
	switch(mat.depth()){
	case CV_8U:
		printf("(CV_8U)");
		break;
	case CV_16S:
		printf("(CV_16S)");
		break;
	case CV_64F:
		printf("(CV_64F)");
		break;
	default:
		printf("(Unknown)");
	};

	printf(",type=%d",mat.type());
	switch(mat.type()){
	case CV_8UC1:
		printf("(CV_8UC1)\n");
		break;
	case CV_8UC3:
		printf("(CV_8UC3)\n");
	  break;
	case CV_8UC4:
		printf("(CV_8UC4)\n");
		break;
	case CV_16SC1:
		printf("(CV_16SC1)\n");
		break;
	case CV_16SC3:
		printf("(CV_16SC3)\n");
		break;
	case CV_16SC4:
		printf("(CV_16SC4)\n");
		break;
	case CV_64FC1:
	  printf("(CV_64FC1)\n");
	  break;
	case CV_64FC3:
		printf("(CV_64FC3)\n");
		break;
	case CV_64FC4:
		printf("(CV_64FC4)\n");
		break;
	default:
		printf("(Unknown)\n");
	};

}

// Poingter input parameters
struct mouseParam {
    int x;
    int y;
    int event;
    int flags;
};

void mouseCallback(int eventType, int x, int y, int flags, void* userdata)
{
    mouseParam *ptr = static_cast<mouseParam*> (userdata);

    ptr->x = x;
    ptr->y = y;
    ptr->event = eventType;
    ptr->flags = flags;
}

#ifdef RASPBERRY_PI
int main(int argc, char * argv[])
#else
int _tmain(int argc, _TCHAR* argv[])
#endif
{

	bool preview = false;
	bool setup_mode = false;
	bool gray_camera = false;
	bool skip_hunting = false;
	clock_t clock_1;
	time_t  start_time;

//	cv::Mat dst;
    mouseParam mouseEvent;

	for(int i=1;i<argc;i++){
		if(!strcmp(argv[i],"-p")){ // capture with preview
			preview=true;
		}
		else if(!strcmp(argv[i],"-s")){	//
			setup_mode = true;
		}
		else if(!strcmp(argv[i],"-n")){	// skip hunting
			skip_hunting = true;
		}
		else {
			fprintf(stderr,"usage: %s [-p][-s]\n",argv[0]);
			return -1;
		}
	}

// Prepare window for preview and setup modes.
	if(preview || setup_mode){
		cvNamedWindow("Preview1");
		cv::setMouseCallback("Preview1", mouseCallback, &mouseEvent);
	};

	if(!setup_camera())
		return -1;	// Camera is not ready

	IplImage *camimage = QUERY_FRAME(cam);
	IplImage *rawimage = cvCloneImage(camimage);
	describe_IplImage(camimage);
	if(camimage->nChannels == 1 && camimage->depth == IPL_DEPTH_8U){
		fprintf(stderr,"Gray camera detected.\n");
		gray_camera = true;
	}
	else {
		fprintf(stderr,"Color camera detected.\n");
		gray_camera = false;
	};

	CvSize sz=cvGetSize(rawimage);
	CvSize hsz = cvSize(sz.width/2, sz.height/2);
	cv::Rect roi(2, hsz.height-64, 8, 192);
	cv::Rect hroi(roi.x/2,roi.y/2,roi.width/2,roi.height/2);
// prepare gray image buffers
	IplImage *image1 = cvCreateImage(sz, IPL_DEPTH_8U,1);
	IplImage *image2 = cvCreateImage(sz, IPL_DEPTH_8U,1);
// prepare preview image buffer
	IplImage *pv = cvCreateImage(hsz, IPL_DEPTH_8U,1);

	if(gray_camera){
		cvCopy(rawimage,image1);
	}
	else {
		cvCvtColor(rawimage,image1,CV_BGR2GRAY);
	};
	describe_IplImage(image1);
/*
	if(preview){
		cvResize(image1,pv);
		cvShowImage("Preview1",pv);
		while(27 != cvWaitKey(20)){
			if (mouseEvent.event == cv::EVENT_LBUTTONDOWN)
				break;
		}
	};


	cvRectangle(image1,cvPoint(roi.x-1, roi.y-1), cvPoint(roi.x+roi.width+1, roi.y+roi.height+1),cvScalar(0,0,0));
	cvRectangle(image1,cvPoint(roi.x, roi.y), cvPoint(roi.x+roi.width, roi.y+roi.height),cvScalar(255,255,255));
	if(preview){
		cvResize(image1,pv);
		cvShowImage("Preview1",pv);
		while(27 != cvWaitKey(20)){
			if (mouseEvent.event == cv::EVENT_LBUTTONDOWN)
				break;
		}
	};
*/

	if(setup_mode){
		fprintf(stderr,"Camera position adjustment...\n");
		while(27 != cvWaitKey(10)){
			if (mouseEvent.event == cv::EVENT_LBUTTONDOWN)
				break;
			camimage = QUERY_FRAME(cam);
			if(gray_camera){
				cvResize(camimage,pv);
			}
			else {
				cvCvtColor(camimage,image2,CV_BGR2GRAY);
				cvResize(image2,pv);
			};
			cvLine(pv,
				cvPoint(0,hsz.height/2+2),cvPoint(hsz.width-1,hsz.height/2+2),
				cvScalar(255,255,255));
			cvLine(pv,
				cvPoint(0,hsz.height/2-2),cvPoint(hsz.width-1,hsz.height/2-2),
				cvScalar(0,0,0));
			cvLine(pv,
				cvPoint(hsz.width/2+2,0),cvPoint(hsz.width/2+2,hsz.height-1),
				cvScalar(255,255,255));
			cvLine(pv,
				cvPoint(hsz.width/2-2,0),cvPoint(hsz.width/2-2,hsz.height-1),
				cvScalar(0,0,0));
			cvShowImage("Preview1",pv);
		}
		RELEASE_CAPTURE(&cam);
		cvReleaseImage(&image1);
		cvReleaseImage(&image2);
		cvReleaseImage(&pv);
		cvReleaseImage(&rawimage);
		return 0;
	}

//	Prepare frame buffers for n_frames
	init_frames(rawimage);

// catch the first frame and copy into image2
	fprintf(stderr,"Start motion hunting..\n");
	camimage = QUERY_FRAME(cam);
	if(gray_camera){
		cvCopy(camimage,image2);
	}
	else {
		cvCvtColor(camimage,image2,CV_BGR2GRAY);
	};

//	average intensity level of roi
	double base_intensity = average(image2,roi);

	fprintf(stderr,"Base intensity = %f\n",base_intensity);
	base_intensity += 0.5;

	double error_max = 0;
	while(27 != cvWaitKey(1)){
		camimage=QUERY_FRAME(cam);
		if(gray_camera){
			cvCopy(camimage,image1);
		}
		else {
			cvCvtColor(camimage,image1,CV_BGR2GRAY);
		};
		double difval = diffsum(image1, image2, roi);
		if(difval > error_max)
			error_max=difval;
		if(128.0*difval/base_intensity > 20)
			break;
		if(preview){
			cvResize(image1,pv);
			cvRectangle(pv,
				cvPoint(hroi.x, hroi.y),
				cvPoint(hroi.x+hroi.width, hroi.y+hroi.height),
				cvScalar(255,255,255));
			cvShowImage("Preview1",pv);
			if (mouseEvent.event == cv::EVENT_LBUTTONDOWN)
				break;
		};
		if(skip_hunting)
			break;
	};

	fprintf(stderr,"Start capture\n");
	start_time = time(NULL);
	clock_1 = clock();
	for(int i=0;i<n_frames;i++){
		camimage=QUERY_FRAME(cam);
		cvCopy(camimage,frames[i]);
		if(preview){
			if(gray_camera){
				cvResize(camimage,pv);
			}
			else {
				cvCvtColor(camimage,image2,CV_BGR2GRAY);
				cvResize(image2,pv);
			};
			cvShowImage("Preview1",pv);
			cvWaitKey(1);
		};
	}

	fprintf(stderr,"Elapse time for capture: %1f sec.  CPU time: %f sec.\n",
		difftime(time(NULL),start_time),
		(double)(clock() - clock_1)/CLOCKS_PER_SEC);

	RELEASE_CAPTURE(&cam);
	char filepath[128];
	start_time = time(NULL);
	clock_1 = clock();
	for(int i=0;i<n_frames;i++){
		sprintf(filepath,"frames/%04d.jpg",i);
		if(gray_camera){
			cvCopy(frames[i],image1);
		}
		else {
			cvCvtColor(frames[i],image1,CV_BGR2GRAY);
		}
		cvSaveImage(filepath,image1);
		cvReleaseImage(&(frames[i]));
		if((i %20)==0)
			fprintf(stderr,"Wrinting %d images\n",i);
	}
	fprintf(stderr,"Elapse time for saving: %1f sec.  CPU time: %f sec.\n",
		difftime(time(NULL),start_time),
		(double)(clock() - clock_1)/CLOCKS_PER_SEC);

	cvReleaseImage(&image1);
	cvReleaseImage(&image2);
	cvReleaseImage(&pv);
	cvReleaseImage(&rawimage);

	if(preview)
		cvDestroyWindow("Preview1");
	fprintf(stderr,"Done.\n");
#ifndef RASPBERRY_PI
	fprintf(stderr,"Hit any key to exit\n");
	getchar();
#endif
	return 0;
}
