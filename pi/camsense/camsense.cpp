// camsense.cpp : コンソール アプリケーションのエントリ ポイントを定義します。

#include "stdafx.h"
#include "math.h"

#define CAM_W 1280
#define CAM_H 768
#define VERBOSE
#define FPS 15
#define ROI_X 350
#define ROI_Y 110
#define ROI_W 380
#define ROI_H 500

#define ZONE1_X 80
#define ZONE1_Y 260
#define ZONE1_W 10
#define ZONE1_H 120

#define ZONE2_X 2
#define ZONE2_Y 160
#define ZONE2_W 10
#define ZONE2_H 120

// Poingter input parameters
struct mouseParam {
	int x;
	int y;
	int event;
	int flags;
};

mouseParam mouseEvent;

int fps = FPS;
int n_frames = FPS*60;
FILE *gpio17, *gpio18;
char _workstr[64];

IplImage *frames[FPS*60];
IplImage *camimage;
IplImage *rawimage;
IplImage *subimage;	// roi cutout
IplImage *image1;
IplImage *image2;
IplImage *pv;


cv::Size sz, hsz;
cv::Rect global_roi(ROI_X,ROI_Y,ROI_W,ROI_H);
cv::Rect zone1(ZONE1_X,ZONE1_Y,ZONE1_W,ZONE1_H);
cv::Rect zone2(ZONE2_X,ZONE2_Y,ZONE2_W,ZONE2_H);

int run_mode = 0;

#ifdef RASPBERRY_PI
// camera initialization for picamera
#define QUERY_FRAME(cam) raspiCamCvQueryFrame(cam)
#define RELEASE_CAPTURE(cam) raspiCamCvReleaseCapture(cam)
#define STRCPY(dst,src) strcpy(dst,src)
#define SPRINTF(dst,fmt,arg1) sprintf(dst,fmt,arg1)

RaspiCamCvCapture *cam = NULL;

bool setup_camera(){
	RASPIVID_CONFIG * config = (RASPIVID_CONFIG*)malloc(sizeof(RASPIVID_CONFIG));
	config->width=CAM_W;
	config->height=CAM_H;
	config->bitrate=0;	// zero: leave as default
	config->framerate=fps;
	config->monochrome=1;
	cam = (RaspiCamCvCapture *)raspiCamCvCreateCameraCapture2(0, config);
	free(config);
	if(cam == NULL){
		printf("Can't open picamera\n");
		return false;
	}
	return true;
}

//	Manupilate gpio port
void setup_gpios()
{
	gpio17=fopen("/sys/class/gpio17/value","+w");	// red LED
	gpio18=fopen("/sys/class/gpio18/value","+w");	// green LED
}

void set_gpio(int port, int value)
{
	switch(port){
	case 17:
		if(gpio17 != NULL)
			fprintf(gpio17,"%d",value);
		break;

	case 18:
		if(gpio17 != NULL)
			fprintf(gpio18,"%d",value);
		break;
	}
}

int get_gpio(int port)
{
	return 0;
}
#else
//	camera initialization for windows
#define QUERY_FRAME(cam) cvQueryFrame(cam)
#define RELEASE_CAPTURE(cam) cvReleaseCapture(cam)
#define STRCPY(dst,src) strcpy_s(dst,sizeof(dst),src)
#define SPRINTF(dst,fmt,arg1) sprintf_s(dst,sizeof(dst),fmt,arg1)

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

//	Manupilate gpio port
void setup_gpios()
{
	gpio17=NULL; // fopen("/sys/class/gpio17/value","+w");	// red LED
	gpio18=NULL; //fopen("/sys/class/gpio18/value","+w");	// green LED
}

void set_gpio(int port, int value)
{
	switch(port){
	case 17:
		if(gpio17 != NULL)
			fprintf(gpio17,"%d",value);
		break;

	case 18:
		if(gpio17 != NULL)
			fprintf(gpio18,"%d",value);
		break;
	}
}

int get_gpio(int port)
{
	return 0;
}

#endif

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
	fprintf(stderr,"dim=%d x %d,nch=%d,depth=%d, imageSize=%d, widthStep=%d\n",
		src->width, src->height, src->nChannels,
		src->depth, src->imageSize, src->widthStep);
}

char *cvDepthName(int depth)
{
	switch(depth){
	case CV_8U:
		STRCPY(_workstr,"CV_8U");
		break;
	case CV_8S:
		STRCPY(_workstr,"CV8S");
		break;
	case CV_16U:
		STRCPY(_workstr,"CV_16U");
		break;
	case CV_16S:
		STRCPY(_workstr,"CV_16S");
		break;
	case CV_32F:
		STRCPY(_workstr,"CV_32F");
		break;
	case CV_64F:
		STRCPY(_workstr,"CV_64F");
		break;
	default:
		SPRINTF(_workstr,"UNKNOWN(%d)",depth);
	};
	return _workstr;
}

void describe_Mat(cv::Mat mat)
{
	fprintf(stderr,"dim=%d x %d, nCh=%d, depth=%s\n",
		mat.cols, mat.rows, mat.channels(),cvDepthName(mat.depth()));

	fprintf(stderr,",type=%d",mat.type());
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
		fprintf(stderr,"(Unknown)\n");
	};
}

void describe_rect(cv::Rect rect)
{
	std::cerr << rect.x << "," << rect.y << ","
			  << rect.width << "," << rect.height << std::endl;
}

void describe_configuration()
{
	std::cerr << "FPS: " << fps << std::endl;
	std::cerr << "ROI: ";
	describe_rect(global_roi);
	std::cerr << "Zone 1: ";
	describe_rect(zone1);
	std::cerr << "Zone 2: ";
	describe_rect(zone2);
}

void mouseCallback(int eventType, int x, int y, int flags, void* userdata)
{
	mouseParam *ptr = static_cast<mouseParam*> (userdata);

	ptr->x = x;
	ptr->y = y;
	ptr->event = eventType;
	ptr->flags = flags;
}

bool preview = false;
bool gray_camera = false;

std::string get_field(std::string src,int pos)
{
	std::string result = "";
	int i=0;
	while(pos > 0){
		while(i<src.length() && src[i] != ',')
			i++;
		if(i >= src.length())
			return result;
		pos--;
		i++;
	}
	while(i<src.length() && src[i] != ','){
		result.push_back(src[i]);
		i++;
	}
	return result;
}

int load_config(char *configpath)
{
	std::ifstream ifs(configpath);
	if(!ifs)
		return -1;

	std::string buf;
	while(!ifs.eof())
	{
		std::getline(ifs,buf);
		if(buf.length() > 0 && buf[0] != '#')
		{
			std::string key = get_field(buf,0);
			std::string val = get_field(buf,1);
			if(key == "FPS"){
				fps = atoi(val.c_str());
			}
			else if(key == "ROI_X"){
				global_roi.x = atoi(val.c_str());
			}
			else if(key == "ROI_Y"){
				global_roi.y = atoi(val.c_str());
			}
			else if(key == "ROI_W"){
				global_roi.width = atoi(val.c_str());
			}
			else if(key == "ROI_H"){
				global_roi.height = atoi(val.c_str());
			}
			else if(key == "ZONE1_X"){
				zone1.x = atoi(val.c_str());
			}
			else if(key == "ZONE1_Y"){
				zone1.y = atoi(val.c_str());
			}
			else if(key == "ZONE1_W"){
				zone1.width = atoi(val.c_str());
			}
			else if(key == "ZONE1_H"){
				zone1.height = atoi(val.c_str());
			}
			else if(key == "ZONE2_X"){
				zone2.x = atoi(val.c_str());
			}
			else if(key == "ZONE2_Y"){
				zone2.y = atoi(val.c_str());
			}
			else if(key == "ZONE2_W"){
				zone2.width = atoi(val.c_str());
			}
			else if(key == "ZONE2_H"){
				zone2.height = atoi(val.c_str());
			}
			else {
				std::cerr <<"Unknown configuration entry:" << buf << std::endl;
				return -1;
			};
		};
	};
	return 0;
}
bool parse_args(int argc, char *argv[])
{
	int i=1;
	char *configpath = (char *)"camsense.conf";

	while(i<argc){
		if(!strcmp(argv[i],"-p") || !strcmp(argv[i],"-preview")){
			// capture with preview
			preview=true;
		}
		else if(!strcmp(argv[i],"-fps")){
			i++;
			if(i<argc)
				fps = atoi(argv[i]);
		}
		else if(!strcmp(argv[i],"-mode")){
			i++;
			if(i<argc)
				run_mode = atoi(argv[i]);
		}
		else if(!strcmp(argv[i],"-config")){
			i++;
			if(i<argc)
				configpath = argv[i];
		}
		else {
			std::cerr<<"usage:"<<argv[0]<<" [-mode n][-config path]\n";
			return false;
		};
		i++;
	};
	load_config(configpath);
	return true;
}

void setup_view()
{
	std::cerr <<"Camera position adjustment...\n";
	cvNamedWindow("Preview1");
	cvMoveWindow("Preview1",50,50);
	cv::setMouseCallback("Preview1", mouseCallback, &mouseEvent);
	int counter=0;
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
		counter++;
	};
}

void init_frames(IplImage *base, int nfr)
{
#ifdef VERBOSE
	std::cerr<<"Frame buffer allocation, size=" <<
			(base->imageSize*nfr)/(1024*1024) << "MB\n";
#endif
	for(int i=0;i<nfr;i++){
		frames[i]=cvCloneImage(base);
		std::cerr << "Frame " << i << std::endl;
	}
}

//	diff image buffer
double diff_sum[ROI_H][ROI_W];

void accumulate_diffsum(IplImage *src1, IplImage *src2)
{
	int p1,p2;
	int rms;
	for(int y=0;y<ROI_H;y++)
		for(int x=0;x<ROI_W;x++){
			p1 = src1->imageData[src1->widthStep*y+x];
			p2 = src2->imageData[src2->widthStep*y+x];
			rms = (p1-p2)*(p1-p2);
			if(rms > 256)
				diff_sum[y][x] += rms;
		};
}

void make_pathimage(cv::Rect roi)
{
	std::cerr << "Start pathfind\n";
	time_t start_time = time(NULL);
	clock_t clock_1 = clock();

//	Clear diff_sum
	for(int iy=0;iy<roi.height;iy++)
		for(int ix=0;ix<roi.width;ix++)
			diff_sum[iy][ix]=0.0;

//	Soaking camera
	for(int i=0;i<fps;i++)
		camimage=QUERY_FRAME(cam);

//	Set first image
	cvSetImageROI(camimage,roi);
	cvCopy(camimage,frames[0]);

	for(int i=0;i<fps*120;i++){
		camimage=QUERY_FRAME(cam);
		cvSetImageROI(camimage,roi);
		cvCopy(camimage,frames[1]);
		accumulate_diffsum(frames[0],frames[1]);

		camimage=QUERY_FRAME(cam);
		cvSetImageROI(camimage,roi);
		cvCopy(camimage,frames[0]);
		accumulate_diffsum(frames[1],frames[0]);
		if(i % (fps*2) == 0)
			std::cerr <<"processing "  << i << " frames\n";
	};

	double diff_max = 1.0;
	for(int iy=0;iy<roi.height;iy++)
		for(int ix=0;ix<roi.width;ix++){
			diff_sum[iy][ix] = sqrt(diff_sum[iy][ix]);
			if(diff_sum[iy][ix] > diff_max)
				diff_max = diff_sum[iy][ix];
		};
	std::cerr <<"diff_max =" << diff_max << std::endl;

	// Normalize to 0:255
	IplImage *dst = frames[0];
	for(int iy=0;iy<roi.height;iy++)
		for(int ix=0;ix<roi.width;ix++){
			int pv = 512*diff_sum[iy][ix]/diff_max;
			if(pv > 255)
				pv = 255;
			dst->imageData[dst->widthStep*iy+ix] = (unsigned char)pv;
		};

	std::cerr <<"Elapse time for capture: "<< difftime(time(NULL),start_time)
	<< " CPU time: " << (double)(clock()-clock_1)/CLOCKS_PER_SEC <<"sec.\n";
}


double diff_rmse(IplImage *src1, IplImage *src2)
{
	double rmse=0.0;
	for(int iy =0; iy<src1->height; iy++)
		for(int ix =0; ix<src1->width; ix++)
		{
			int p1= (double)(src1->imageData[src1->widthStep*iy+ix]);
			int p2= (double)(src2->imageData[src2->widthStep*iy+ix]);
			rmse += (double)((p1-p2)*(p1-p2));
		};
	return rmse/(src1->width * src1->height);
}

void sense_log(int x, int y, int w, int h)
{

	cv::Rect roi(x,y,w,h);
//	Soaking camera
	for(int i=0;i<fps;i++)
		camimage=QUERY_FRAME(cam);

//	Set first image
	cvSetImageROI(camimage,roi);
	cvCopy(camimage,frames[0]);

	for(int i=0;i<fps*60;i += 2){
		camimage=QUERY_FRAME(cam);
		cvSetImageROI(camimage,roi);
		cvCopy(camimage,frames[1]);
		double rmse1 = diff_rmse(frames[0],frames[1]);

		camimage=QUERY_FRAME(cam);
		cvSetImageROI(camimage,roi);
		cvCopy(camimage,frames[0]);
		double rmse2 = diff_rmse(frames[1],frames[0]);

		printf("%d,%g\n%d,%g\n",i,rmse1,i+1,rmse2);
	};

}

void capture_target(cv::Rect roi)
{
	std::cerr << "Start capture\n";
	time_t start_time = time(NULL);
	clock_t clock_1 = clock();
	for(int i=0;i<n_frames;i++){
		camimage=QUERY_FRAME(cam);
		cvSetImageROI(camimage,roi);
		cvCopy(camimage,frames[i]);
	}

	std::cerr <<"Elapse time for capture: "<< difftime(time(NULL),start_time)
	<< " CPU time: " << (double)(clock()-clock_1)/CLOCKS_PER_SEC<<"sec.\n";
}

char *datetimestr(char *buf, int size)
{
	time_t now;
	struct tm *ts;
	now = time(NULL);
	ts = localtime(&now);
	strftime(buf,size,"%Y-%m-%dT%H%M%S",ts);
}

void filename_by_time(char *buf, int size,time_t *timestamp)
{
	struct tm *ts = localtime(timestamp);
	strftime(buf,size,"traffics/%Y-%m-%dT%H%M%S.png",ts);
}

int calc_diffsumx(IplImage *img0, IplImage *img1, cv::Rect roi,int iy)
{
	uchar *p0 =(uchar *)&(img0->imageData[img0->widthStep*(roi.y+iy)+roi.x]);
	uchar *p1 =(uchar *)&(img1->imageData[img1->widthStep*(roi.y+iy)+roi.x]);
	int diffsum = 0;
	for(int x=0;x<roi.width;x++)
	{
		diffsum += (*p0 - *p1)*(*p0 - *p1);
		p0++;
		p1++;
	}
	diffsum = (diffsum/roi.width)/2 - 64;
	if(diffsum < 0)
		return 0;
	if(diffsum > 255)
		return 255;
	return diffsum;
}

void set_roadimg(IplImage *src, IplImage *dst, cv::Rect roi,int dst_y, int dst_x)
{
	uchar *dp = (uchar *)&(dst->imageData[dst->widthStep*dst_y + dst_x]);
	for(int y=roi.y; y<roi.y+roi.height; y++)
	{
		int sum = 0;
		uchar *sp = (uchar *)&(src->imageData[src->widthStep*y + roi.x]);
		for(int i=0;i<roi.width;i++)
		{
			sum += *sp++;
		};
		*dp++ = sum/roi.width;
	};

}

void measure_traffic(int debug)
{

	char filename[64];
	std::cerr << "Start capture\n";

	IplImage *img0 = cvCreateImage(cv::Size(global_roi.width, global_roi.height), IPL_DEPTH_8U,1);
	IplImage *img1 = cvCreateImage(cv::Size(global_roi.width, global_roi.height), IPL_DEPTH_8U,1);

	cv::Size sz_out(ZONE1_H+ZONE2_H,fps*60);
	IplImage *trafficmap = cvCreateImage(sz_out, IPL_DEPTH_8U,1);
	IplImage *roadimgmap = cvCreateImage(sz_out, IPL_DEPTH_8U,1);

	time_t start_time = time(NULL);
	struct tm *ts = localtime(&start_time);
	fprintf(stderr,"Starting at: %2d:%2d:%2d\n",ts->tm_hour,ts->tm_min,ts->tm_sec);

//	Sync to 00 sec.
	int prev_sec = 0;
	for(int i=0; i<fps*120;i++){
		camimage=QUERY_FRAME(cam);
		start_time = time(NULL);
		struct tm *ts = localtime(&start_time);
		if(prev_sec = 59 && ts->tm_sec ==  0){
			for(int j=0;j<fps/2;j++)
				camimage=QUERY_FRAME(cam);
			break;
		}
		prev_sec = ts->tm_sec;
	}

//	At this point, tm_sec == 0

	cvSetImageROI(camimage,global_roi);
//	cvSmooth(camimage,img0, 3, 0, 0, 0);
	cvCopy(camimage,img0);

	clock_t clock_1 = clock();
	for(;;){
		start_time = time(NULL);
		for(int i=0;i<fps*60;i++)
		{
			camimage=QUERY_FRAME(cam);
			cvSetImageROI(camimage,global_roi);
//			cvSmooth(camimage,img1, 3, 0, 0, 0);
			cvCopy(camimage,img1);
			for(int iy=0;iy<zone1.height;iy++)
			{
				unsigned char diffsum = calc_diffsumx(img0,img1,zone1,iy);
				trafficmap->imageData[trafficmap->widthStep*i+iy]=diffsum;

				diffsum = calc_diffsumx(img0,img1,zone2,iy);
				trafficmap->imageData[trafficmap->widthStep*i+iy+ZONE1_H]=diffsum;
			};

			set_roadimg(img1,roadimgmap,zone1,i,0);
			set_roadimg(img1,roadimgmap,zone2,i,zone1.height);

			cvCopy(img1,img0);
//			if(i % (fps*2) == 0)
//				fprintf(stderr,"%d",i/fps);
		}
		char dtstr[32];
		datetimestr(dtstr,sizeof(dtstr));
		sprintf(filename,"traffics/%s.png",dtstr);
		filename_by_time(filename,sizeof(filename),&start_time);
		cvSaveImage(filename,trafficmap);
		sprintf(filename,"traffics/%s.jpg",dtstr);
		cvSaveImage(filename,roadimgmap);
		fprintf(stderr,"%s saved\n",dtstr);
		if(debug != 0)
			break;
	}
	fprintf(stderr,"Elapse time for capture: %1f sec.  CPU time: %f sec.\n",
		difftime(time(NULL),start_time),
		(double)(clock() - clock_1)/CLOCKS_PER_SEC);
}

void write_images()
{
	char filepath[128];
	time_t start_time = time(NULL);
	clock_t clock_1 = clock();

	for(int i=0;i<n_frames;i++){
		SPRINTF(filepath,"frames/%04d.jpg",i);
		if(gray_camera){
			cvCopy(frames[i],subimage);
		}
		else {
			cvCvtColor(frames[i],subimage,CV_BGR2GRAY);
		}
		cvSaveImage(filepath,subimage);
		cvReleaseImage(&(frames[i]));
		if((i %20)==0)
			fprintf(stderr,"Wrinting %d images\n",i);
	}
	fprintf(stderr,"Elapse time for saving: %1f sec.  CPU time: %f sec.\n",
		difftime(time(NULL),start_time),
		(double)(clock() - clock_1)/CLOCKS_PER_SEC);
}


#ifdef RASPBERRY_PI
int main(int argc, char * argv[])
#else
int _tmain(int argc, _TCHAR* argv[])
#endif
{
	char message_buf[256];
	int message_size;

	if(!parse_args(argc,argv))
		return -1;
	describe_configuration();

//	setup_gpios();
//	set_gpio(17,0);	// red LED off
//	set_gpio(18,0);	// green LED off

	if(!setup_camera())
		return -1;	// Camera is not ready

	camimage = QUERY_FRAME(cam);
	rawimage = cvCloneImage(camimage);
//	describe_IplImage(camimage);

	if(camimage->nChannels == 1 && camimage->depth == IPL_DEPTH_8U){
		fprintf(stderr,"Gray camera detected.\n");
		gray_camera = true;
	}
	else {
		fprintf(stderr,"Color camera detected.\n");
		gray_camera = false;
	};

	sz = cvGetSize(rawimage);
	hsz = cv::Size(sz.width/2, sz.height/2);

// prepare gray image buffers
	image1 = cvCreateImage(sz, IPL_DEPTH_8U,1);
	image2 = cvCreateImage(sz, IPL_DEPTH_8U,1);

// prepare preview image buffer
	pv = cvCreateImage(hsz, IPL_DEPTH_8U,1);

	if(gray_camera){
		cvCopy(rawimage,image1);
	}
	else {
		cvCvtColor(rawimage,image1,CV_BGR2GRAY);
	};

	describe_IplImage(image1);

	switch(run_mode){
		case 0:
			setup_view();
			break;

		case 1:
			subimage = cvCreateImage(cv::Size(ROI_W,ROI_H), IPL_DEPTH_8U,1);
			init_frames(subimage,n_frames);
			capture_target(global_roi);
			write_images();
			break;

		case 2:
			subimage = cvCreateImage(cv::Size(ROI_W,ROI_H), IPL_DEPTH_8U,1);
			init_frames(subimage,3);	// use 3 frames
			make_pathimage(global_roi);
			cvSaveImage("pathimage.jpg",frames[0]);
			break;

		case 3:
			subimage=cvCreateImage(cv::Size(ZONE1_W,ZONE1_H),IPL_DEPTH_8U,1);
			init_frames(subimage,3);	// use 3 frames
			sense_log(ROI_X+ZONE1_X, ROI_Y+ZONE1_Y, ZONE1_W, ZONE1_H);
			break;

		case 4:
			measure_traffic(0);
			break;

		case 5:
			measure_traffic(1);
			break;

	};

	RELEASE_CAPTURE(&cam);
	cvReleaseImage(&image1);
	cvReleaseImage(&image2);
	cvReleaseImage(&pv);
	cvReleaseImage(&rawimage);

	if(preview)
		cvDestroyWindow("Preview1");

#ifndef RASPBERRY_PI
	fprintf(stderr,"Hit any key to exit\n");
	getchar();
#endif

	return 0;
}
