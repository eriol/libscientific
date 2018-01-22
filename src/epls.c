/* epls.c
*
* Copyright (C) <2018>  Giuseppe Marco Randazzo
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "epls.h"

#include "memwrapper.h"
#include "pls.h"
#include "pca.h" /*Using: MatrixAutoScaling(); and calcVarExpressed(); */
#include "numeric.h" /* Using:  if(FLOAT_EQ(NumOne, NumTwo));*/
#include "metricspace.h"
#include "modelvalidation.h"
#include <math.h>

void NewEPLSModel(EPLSMODEL** m)
{
  (*m) = xmalloc(sizeof(EPLSMODEL));
  (*m)->models = NULL;
  (*m)->model_feature_ids = NULL;
  (*m)->c1_validation = NULL;
  (*m)->c2_validation = NULL;
  (*m)->n_models = 0;
  (*m)->nlv = 0;
  (*m)->ny = 0;
}

void DelEPLSModel(EPLSMODEL** m)
{
  size_t i;
  for(i = 0; i < (*m)->n_models; i++){
    DelPLSModel(&(*m)->models[i]);
    if((*m)->model_feature_ids != NULL)
      DelUIVector(&(*m)->model_feature_ids[i]);

    if((*m)->c1_validation != NULL)
      DelMatrix(&(*m)->c1_validation[i]);
    if((*m)->c2_validation != NULL)
      DelMatrix(&(*m)->c2_validation[i]);
  }
  if((*m)->model_feature_ids != NULL)
    xfree((*m)->model_feature_ids);
  if((*m)->c1_validation != NULL)
    xfree((*m)->c1_validation);
  if((*m)->c2_validation != NULL)
    xfree((*m)->c2_validation);
  xfree((*m)->models);
  xfree((*m));
}


void SubspaceMatrix(matrix *mx, uivector *featureids, matrix **x_subspace)
{
  size_t i, j, p;
  p = featureids->size;
  if(mx->row != (*x_subspace)->row && p != (*x_subspace)->col)
    ResizeMatrix(x_subspace, mx->row, p);

  for(j = 0; j < p; j++){
    size_t j_id = featureids->data[j];
    for(i = 0; i < mx->row; i++){
      (*x_subspace)->data[i][j] = mx->data[i][j_id];
    }
  }
}

void EPLS(matrix *mx, matrix *my, size_t nlv, size_t xautoscaling, size_t yautoscaling, EPLSMODEL *m, ELearningParameters eparm, ssignal *s)
{
  size_t i, j, it;
  /* Model allocation */
  m->models = xmalloc(sizeof(PLSMODEL)*eparm.n_models);
  /*m->c1_validation = xmalloc(sizeof(matrix)*eparm.n_models);
  m->c2_validation = xmalloc(sizeof(matrix)*eparm.n_models);*/
  m->n_models = eparm.n_models;
  if(eparm.r_fix < nlv)
    m->nlv = eparm.r_fix;
  else
    m->nlv = nlv;
  m->ny = my->col;

  if(eparm.algorithm == Bagging){
    matrix *x_train, *y_train, *x_test, *y_test;
    uivector *testids;
    double testsize = 1 - eparm.trainsize;
    unsigned int srand_init = mx->row *testsize + xautoscaling + yautoscaling + mx->col + my->col;
    for(it = 0; it < eparm.n_models; it++){
      initMatrix(&x_train);
      initMatrix(&y_train);
      initMatrix(&x_test);
      initMatrix(&y_test);
      initUIVector(&testids);
      train_test_split(mx, my, testsize, &x_train, &y_train, &x_test, &y_test, &testids, &srand_init);
      srand_init++;
      NewPLSModel(&m->models[it]);
      PLS(x_train, y_train, nlv, xautoscaling, yautoscaling, m->models[it], s);
      /*initMatrix(&p_y_test);
      PLSYPredictorAllLV(x_test, m->models[it], nlv, &p_y_test);
      initMatrix(&m->c1_validation[it]);
      initMatrix(&m->c2_validation[it]);

      if(emt == Regression)
        PLSRegressionStatistics(y_test, p_y_test, &m->c1_validation[it], &m->c2_validation[it], NULL);
      else
        PLSDiscriminantAnalysisStatistics(y_test, p_y_test, NULL, &m->c1_validation[it], NULL, &m->c2_validation[it]);
      */
      DelUIVector(&testids);
      DelMatrix(&x_train);
      DelMatrix(&y_train);
      DelMatrix(&x_test);
      DelMatrix(&y_test);
    }
  }
  else{
    /* Random Subspace Method
     * - algorithm at fixed subset size
     * - algorithm at growing subset size
     */
     matrix *x_train, *x_subspace, *y_train, *x_test, *y_test;
     uivector *testids;
     double testsize = 1;
     unsigned int srand_init = mx->row *testsize + xautoscaling + yautoscaling + mx->col + my->col;

     /*Create a vector with all the columns id*/
     uivector *featureids;
     NewUIVector(&featureids, mx->col);
     for(i = 0; i < featureids->size; i++){
       featureids->data[i] = i;
     }


     m->model_feature_ids = xmalloc(sizeof(uivector*)*eparm.n_models);

     if(eparm.algorithm == FixedRandomSubspaceMethod){
       NewMatrix(&x_subspace, mx->row, eparm.r_fix);
     }
     else{ // (eparm.algorithm == BaggingRandomSubspaceMethod){
       testsize = 1 - eparm.trainsize;
       initMatrix(&x_subspace);
    }

     /* Create a random id vector */
     for(it = 0; it < eparm.n_models; it++){
       /* Sattolo's algorithm to shuffle the featureids*/
       i = featureids->size;
       while(i > 1){
         i = i - 1;
         j = randInt(0, i);
         double tmp = featureids->data[j];
         featureids->data[j] = featureids->data[i];
         featureids->data[i] = tmp;
       }

       NewUIVector(&m->model_feature_ids[it], eparm.r_fix);
       /* Copy the column ids for future predictions */
       for(j = 0; j < eparm.r_fix; j++){
         m->model_feature_ids[it]->data[j] = featureids->data[j];
       }

       if(eparm.algorithm == FixedRandomSubspaceMethod){
         SubspaceMatrix(mx, m->model_feature_ids[it], &x_subspace);
         NewPLSModel(&m->models[it]);
         PLS(x_subspace, my, nlv, xautoscaling, yautoscaling, m->models[it], s);
       }
       else{

         initMatrix(&x_train);
         initMatrix(&y_train);
         initMatrix(&x_test);
         initMatrix(&y_test);
         initUIVector(&testids);
         train_test_split(mx, my, testsize, &x_train, &y_train, &x_test, &y_test, &testids, &srand_init);
         srand_init++;
         SubspaceMatrix(x_train, m->model_feature_ids[it], &x_subspace);
         NewPLSModel(&m->models[it]);
         PLS(x_subspace, y_train, nlv, xautoscaling, yautoscaling, m->models[it], s);
         DelMatrix(&x_train);
         DelMatrix(&y_train);
         DelMatrix(&x_test);
         DelMatrix(&y_test);
         DelUIVector(&testids);
       }
     }
     DelUIVector(&featureids);
     DelMatrix(&x_subspace);
  }
}

void EPLSGetSXScore(EPLSMODEL *m, CombinationRule crule, matrix *sxscores){}
void EPLSGetSXLoadings(EPLSMODEL *m, CombinationRule crule, matrix *sxloadings){}
void EPLSGetSYScore(EPLSMODEL *m, CombinationRule crule, matrix *syscores){}
void EPLSGetSYLoadings(EPLSMODEL *m, CombinationRule crule, matrix *syloadings){}
void EPLSGetSWeights(EPLSMODEL *m, CombinationRule crule, matrix *sweights){}
void EPLSGetSBetaCoefficients(EPLSMODEL *m, CombinationRule crule, matrix *sbetas){}

void EPLSYPRedictorAllLV(matrix *mx, EPLSMODEL *m, CombinationRule crule, array **tscores, matrix **py)
{
  size_t i, j, k;
  ResizeMatrix(py, mx->row, m->ny*m->nlv);
  matrix *x_subspace;
  initMatrix(&x_subspace);
  if(crule == Averaging){
    matrix *model_py;
    matrix *tscores_;
    for(i = 0; i < m->n_models; i++){
      initMatrix(&model_py);
      initMatrix(&tscores_);
      if(m->model_feature_ids != NULL){
        //PrintUIVector(m->model_feature_ids[i]);
        SubspaceMatrix(mx, m->model_feature_ids[i], &x_subspace);
        PLSYPredictorAllLV(x_subspace,  m->models[i], &tscores_, &model_py);
      }
      else{
        PLSYPredictorAllLV(mx,  m->models[i], &tscores_, &model_py);
      }

      if(tscores != NULL){
        ArrayAppendMatrix(tscores, tscores_);
      }

      for(k = 0; k < model_py->row; k++){
        for(j = 0; j < model_py->col; j++){
          (*py)->data[k][j] += model_py->data[k][j];
        }
      }
      DelMatrix(&model_py);
    }

    /* Averaging the result */
    for(i = 0; i < (*py)->row; i++){
      for(j = 0; j < (*py)->col; j++){
        (*py)->data[i][j] /= (double)m->n_models;
      }
    }
  }
  else if(crule == Median){
    dvector *v;
    array *model_py;
    matrix *tscores_;
    initArray(&model_py);
    for(i = 0; i < m->n_models; i++){
      initMatrix(&tscores_);
      AddArrayMatrix(&model_py, mx->row, m->ny*m->nlv);
      if(m->model_feature_ids != NULL){
        SubspaceMatrix(mx, m->model_feature_ids[i], &x_subspace);
        PLSYPredictorAllLV(x_subspace,  m->models[i], &tscores_, &model_py->m[i]);
      }
      else{
        PLSYPredictorAllLV(mx,  m->models[i], &tscores_, &model_py->m[i]);
      }

      if(tscores != NULL)
        ArrayAppendMatrix(tscores, tscores_);

      DelMatrix(&tscores_);
    }

    NewDVector(&v, m->n_models);
    for(j = 0; j < m->ny*m->nlv; j++){
      for(i = 0; i < mx->row; i++){
        for(k = 0; k < model_py->order; k++){
          v->data[k] = model_py->m[k]->data[i][j];
        }
        double median;
        DVectorMedian(v, &median);
        (*py)->data[i][j] = median;
      }
    }
    DelDVector(&v);
    DelArray(&model_py);
  }
  else{
    return;
  }
  DelMatrix(&x_subspace);
}

void EPLSRegressionStatistics(matrix *my_true, matrix *my_pred, matrix** ccoeff, matrix **stdev, matrix **bias)
{
  /* recall to PLS method */
  PLSRegressionStatistics(my_true, my_pred, ccoeff, stdev, bias);
}

void EPLSDiscriminantAnalysisStatistics(matrix *my_true, matrix *my_score, array **roc, matrix **roc_auc, array **precision_recall, matrix **precision_recall_ap)
{
  /*recall to PLS method */
  PLSDiscriminantAnalysisStatistics(my_true, my_score, roc, roc_auc, precision_recall, precision_recall_ap);
}

void PrintEPLSModel(EPLSMODEL *m)
{
  size_t i, j, k;
  for(i = 0; i < m->n_models; i++){
    PrintPLSModel(m->models[i]);
  }

  if(m->c1_validation != NULL){
    for(j = 0; j < m->n_models; j++){
      PrintMatrix(m->c1_validation[j]);
    }


    for(k = 0; k < m->ny; k++){
      printf("Y No. %lu\n", k);
      puts("Q2 or AUC external");
      for(i = 0; i < m->nlv; i++){
        printf("%lu;", i+1);
        for(j = 0; j < m->n_models-1; j++){
          printf("%f;", m->c1_validation[j]->data[i][k]);
        }
        printf("%f\n", m->c1_validation[m->n_models-1]->data[i][k]);
      }
      puts("SDEP or PrecisionRecall Average external");
      for(i = 0; i < m->nlv; i++){
        printf("%lu;", i+1);
        for(j = 0; j < m->n_models-1; j++){
          printf("%f;", m->c2_validation[j]->data[i][k]);
        }
        printf("%f\n", m->c2_validation[m->n_models-1]->data[i][k]);
      }
      printf(">>> END\n");
    }
  }
}