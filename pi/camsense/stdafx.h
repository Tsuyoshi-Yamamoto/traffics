// stdafx.h : 標準のシステム インクルード ファイルのインクルード ファイル、または
// 参照回数が多く、かつあまり変更されない、プロジェクト専用のインクルード ファイル
// を記述します。
//
#define RASPBERRY_PI

#pragma once

#ifndef RASPBERRY_PI

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>

// TODO: プログラムに必要な追加ヘッダーをここで参照してください。
#include <time.h>
#include <opencv2/opencv.hpp>
#include <opencv2/opencv_lib.hpp>

#else
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include <cv.h>
#include <highgui.h>

#include "RaspiCamCV.h"
#endif

#include <iostream>
#include <fstream>

