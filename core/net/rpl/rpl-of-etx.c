/**
 * \addtogroup uip6
 * @{
 */
/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */
/**
 * \file
 *         The minrank-hysteresis objective function (OCP 1).
 *
 *         This implementation uses the estimated number of 
 *         transmissions (ETX) as the additive routing metric.
 *
 *         MRHOF + ETX computes the path with minor tx numbers from a node to the root
 *
 * \author Joakim Eriksson <joakime@sics.se>, Nicolas Tsiftes <nvt@sics.se>
 */


/* Modified by RMonica
 *
 * patches: - different nodes may have different cycle times
 *            (only for the addition of one line to update_metric_container,
 *            to broadcast the cycle time)
 *          - add RPL function RPL_DAG_MC_AVG_DELAY
 *            (search for RPL_DAG_MC_AVG_DELAY to see modifications)
 */

/* Modified by FDemicheli
* - add Energest function RPL_DAG_MC_EN_TOT
*   (search for RPL_DAG_MC_MEN_TOT to see modifications)
*/

#include "net/rpl/rpl-private.h"
#include "net/neighbor-info.h"
#include "sys/compower.h" ///Added by FDemicheli
#include "sys/energest.h" ///Added by FDemicheli
//#include "node-id.h"

#define DEBUG DEBUG_NONE
//#define DEBUG DEBUG_PRINT

#include "net/uip-debug.h"

static void reset(rpl_dag_t *);
static void parent_state_callback(rpl_parent_t *, int, int);
static rpl_parent_t *best_parent(rpl_parent_t *, rpl_parent_t *);
static rpl_dag_t *best_dag(rpl_dag_t *, rpl_dag_t *);
static rpl_rank_t calculate_rank(rpl_parent_t *, rpl_rank_t);

/*
 Calculates a rank value using the parent rank and a base rank.  If "parent" is NULL, the objective function selects a default increment
 that is adds to the "base_rank". Otherwise, the OF uses information known  about "parent" to select an increment to the "base_rank".
*/

static void update_metric_container(rpl_instance_t *);

rpl_of_t rpl_of_etx = {
  reset,
  parent_state_callback,
  best_parent,
  best_dag,
  calculate_rank,
  update_metric_container,
  1
};

/* Reject parents that have a higher link metric than the following. */
#define MAX_LINK_METRIC			10 /*disabilita i collegamenti con più di 10 tx sul cammino selezionato */

/* Reject parents that have a higher path cost than the following. */
#define MAX_PATH_COST			100 /*disabilita i cammini che necessitano di più di 100 tx*/

/* PARENT_SWITCH_THRESHOLD = la differenza tra il costo del cammino attraverso il genitore preferito e il minimo costo del cammino
                             al fine di attivare la selezione di un nuovo genitore preferito
                             
 * The rank must differ more than 1/PARENT_SWITCH_THRESHOLD_DIV in order
 * to switch preferred parent.
 */
#define PARENT_SWITCH_THRESHOLD_DIV	2 /*x discriminare i due rank con ETX*/

#define RPL_PARENT_SWITCH_THRESHOLD	5 /*threshold con ENTOT */
//#define RPL_PARENT_SWITCH_THRESHOLD	10 /*threshold con ENTOT */

#define AVG_DELAY_MAX_DELAY 65535 /*RMonica*/
#define AVG_DELAY_SWITCH_THRESHOLD (RTIMER_ARCH_SECOND / 3000) /*RMonica*/

typedef uint16_t rpl_path_metric_t;

static rpl_path_metric_t
calculate_path_metric(rpl_parent_t *p)
{
#if (RPL_DAG_MC == RPL_DAG_MC_ETX)
  if(p == NULL || (p->mc.obj.etx == 0 && p->rank > ROOT_RANK(p->dag->instance))) {
    return MAX_PATH_COST * RPL_DAG_MC_ETX_DIVISOR;
  } else {    
    long etx = p->link_metric;//all'inizio vale INITIAL_LINK_METRIC
  //  PRINTF("link_metric in rpl-of-etx = %ld\n", etx);
    etx = (etx * RPL_DAG_MC_ETX_DIVISOR) / NEIGHBOR_INFO_ETX_DIVISOR; //in sostanza: etx = etx * 8
   // PRINTF("etx = %lu\n", etx);
//     PRINTF("p->mc.obj.etx = %u\n", p->mc.obj.etx);
//     PRINTF("(uint16_t) etx = %d\n", (uint16_t) etx);
   // PRINTF("p->mc.obj.etx + (uint16_t) etx = %d\n", p->mc.obj.etx + (uint16_t) etx);
    return p->mc.obj.etx + (uint16_t) etx;
  }
#elif RPL_DAG_MC == RPL_DAG_MC_AVG_DELAY // RMonica
  if(p == NULL || (p->mc.obj.avg_delay_to_sink == 0 && p->rank > ROOT_RANK(p->dag->instance))) {
    return AVG_DELAY_MAX_DELAY;
  } else {
    rimeaddr_t macaddr;
    uip_ds6_get_addr_iid(&(p->addr),(uip_lladdr_t *)&macaddr);   
    long delay = contikimac_get_average_delay_for_routing(&macaddr) >> 4;    
    //printf("calculate_path_metric: %lu to %u\n",delay,(int)(macaddr.u8[7]));
    delay += p->mc.obj.avg_delay_to_sink;
    //printf(" total: %lu\n",delay);
    if (delay > AVG_DELAY_MAX_DELAY) {
      delay = AVG_DELAY_MAX_DELAY;
    }
    return (rpl_path_metric_t)delay;
  }

#elif (RPL_DAG_MC == RPL_DAG_MC_EN_TOT) ///FDemicheli
  if(p == NULL || (p->mc.obj.entot == 0 && p->rank > ROOT_RANK(p->dag->instance))) {
    //consumo iniziale: se il parent non consuma, almeno spedisco il consumo del nodo corrente (che puo essere anch'esso 0)
    //PRINTF("PARENT NON CONSUMA\n");
    //PRINTF("consumed_energy = %u\n",energest_get_current_energy_consumption());
     return p->mc.obj.entot + energest_get_current_energy_consumption();
  } else {
    uint16_t consumed_energy = energest_get_current_energy_consumption();///node's energy consumption
    //PRINTF("my consumed_energy = %u\n",consumed_energy);
    //Devo andare a leggere il valore di energia consumata del nodo parent
    ///p->mc.obj.entot; //energy consumption from parent node --> reads from DIO message received    
    //PRINTF("p->mc.obj.entot = %u\n",p->mc.obj.entot);
    ///total energy consumption
    return p->mc.obj.entot + consumed_energy;//--> è questo che viene inserito nel DIO x essere spedito
  }
  
#else

#error "calculate_path_metric: Not supported."

#endif
}

static void
reset(rpl_dag_t *sag)
{
  //Resets the objective function state for a specific DAG. This function is called when doing a global repair on the DAG.
}

static void
parent_state_callback(rpl_parent_t *parent, int known, int etx)
{
  //Receives link-layer neighbor information. The parameter "known" is set either to 0 or 1. The "etx" parameter specifies the current
 //ETX(estimated transmissions) for the neighbor.
}

static rpl_rank_t
calculate_rank(rpl_parent_t *p, rpl_rank_t base_rank) 
{
  rpl_rank_t new_rank;
  rpl_rank_t rank_increase;

  if(p == NULL) {
    if(base_rank == 0) {
      return INFINITE_RANK;
    }
    rank_increase = NEIGHBOR_INFO_FIX2ETX(INITIAL_LINK_METRIC) * RPL_MIN_HOPRANKINC; //valore intero
    //rank_increase = NEIGHBOR_INFO_FIX2ETX(80) * 256 = (80/16) * 256 = 5 * 256 = 1280
    //PRINTF("rank_increase (1280) = %d\n",rank_increase);
  } else {
    /* multiply first, then scale down to avoid truncation effects */
#if (RPL_DAG_MC == RPL_DAG_MC_ETX) 
    rank_increase = NEIGHBOR_INFO_FIX2ETX(p->link_metric * p->dag->instance->min_hoprankinc);//valore intero
    //rank_increase = p->link_metric * 256 = ETX * 256
    //PRINTF("rank_increase = %d\n",rank_increase);
#elif(RPL_DAG_MC == RPL_DAG_MC_ENERGY)    
    rank_increase = NEIGHBOR_INFO_FIX2ETX(p->dag->instance->min_hoprankinc);
#elif RPL_DAG_MC == RPL_DAG_MC_AVG_DELAY
    rank_increase = NEIGHBOR_INFO_FIX2ETX(p->dag->instance->min_hoprankinc);
    
#elif (RPL_DAG_MC_EN_TOT == RPL_DAG_MC_EN_TOT)
    rank_increase = NEIGHBOR_INFO_FIX2ETX( 16 * p->dag->instance->min_hoprankinc);
    ///questo valore consente di rilevare i loop e risolverli.
#else
#error "calculate_rank: not supported."
#endif
    if(base_rank == 0) {
      base_rank = p->rank; ///base_rank è in sostanza il rank del parent
      //PRINTF("base rank = %d\n", base_rank);
    }
  }

  if(INFINITE_RANK - base_rank < rank_increase) {
    /* Reached the maximum rank. */
    new_rank = INFINITE_RANK;
  } else {
   /* Calculate the rank based on the new rank information from DIO or
      stored otherwise. */
    new_rank = base_rank + rank_increase; ///parent_rank + dipende
  }
  
  return new_rank;   
}

static rpl_dag_t *
best_dag(rpl_dag_t *d1, rpl_dag_t *d2)
{
  if(d1->grounded != d2->grounded) {
    return d1->grounded ? d1 : d2;
  }

  if(d1->preference != d2->preference) {
    return d1->preference > d2->preference ? d1 : d2;
  }

  return d1->rank < d2->rank ? d1 : d2;
}

static rpl_parent_t *
best_parent(rpl_parent_t *p1, rpl_parent_t *p2) //Compares two parents and returns the best one, according to the OF.
{  
  rpl_dag_t *dag;
 
  rpl_path_metric_t min_diff;
  
  rpl_path_metric_t p1_metric;
  rpl_path_metric_t p2_metric;
 
  dag = p1->dag; // Both parents must be in the same DAG. 

#if (RPL_DAG_MC == RPL_DAG_MC_ETX)
  min_diff = RPL_DAG_MC_ETX_DIVISOR / PARENT_SWITCH_THRESHOLD_DIV;//min_diff = 128/2 = 64
#elif (RPL_DAG_MC == RPL_DAG_MC_AVG_DELAY)
  min_diff = AVG_DELAY_SWITCH_THRESHOLD;//10
#elif (RPL_DAG_MC == RPL_DAG_MC_ENERGY)
  min_diff = 0;
#elif (RPL_DAG_MC == RPL_DAG_MC_EN_TOT)
  min_diff = RPL_PARENT_SWITCH_THRESHOLD;
#else
#error "best_parent: RPL_DAG_MC not supported."
#endif

/*    PRINTF("P1 = "); //--> è il primo nodo nell'insieme dei parent
    PRINT6ADDR(&p1->addr);
    PRINTF("\n");

    PRINTF("P2 = "); //--> è il primo nodo nell'insieme dei parent
    PRINT6ADDR(&p2->addr);
    PRINTF("\n");
    
  
   PRINTF("p1 rank = %u\n", p1->rank);
   PRINTF("p2 rank = %u\n",p2->rank);
   
   p1_metric = calculate_path_metric(p1); 
   PRINTF("p1_metric = %u\n", p1_metric);
    
   p2_metric = calculate_path_metric(p2);
   PRINTF("p2_metric = %u\n", p2_metric);*/
   
  if(p1 == dag->preferred_parent || p2 == dag->preferred_parent) {      
   // Maintain stability of the preferred parent in case of similar ranks. 
#if (RPL_DAG_MC == RPL_DAG_MC_EN_TOT) 

// PIETRO: se la differenza è minore della soglia, ritorna il genitore preferito (che è uno dei due)
  
    if(p1_metric == 0 || p2_metric == 0 || p1_metric == p2_metric){
      return dag->preferred_parent;
    }
    else {
      if((p1_metric > p2_metric && ((p1_metric - p2_metric) / p2_metric)*100 < min_diff) ||
	(p2_metric > p1_metric && ((p2_metric - p1_metric) / p1_metric)*100 < min_diff)){
       
	return dag->preferred_parent; // manteniamo il preferred parent attuale se la differenza è minore della soglia min_diff
      } 
    }
#elif ((RPL_DAG_MC == RPL_DAG_MC_ETX) || (RPL_DAG_MC == RPL_DAG_MC_ENERGY) || (RPL_DAG_MC == RPL_DAG_MC_AVG_DELAY))      
  if(p1_metric < p2_metric + min_diff &&
               p1_metric > p2_metric - min_diff){         
      return dag->preferred_parent;
      
    } 
#else
#error "best_parent: RPL_DAG_MC not supported."
#endif
 }

#if (RPL_DAG_MC == RPL_DAG_MC_EN_TOT) /**riscontrato durante le simulazioni all'inizio se i due valori di energia consumata sono uguali.
Se due nodi hanno la stessa metrica, viene scelto P2 dal SO; però P2 non sempre è la scelta corretta. Allora rimango con p1.
TODO: da sistemare*/
if(p1_metric == p2_metric)
{
      return p1;
}     
#endif
 // se nessuno dei due è il preferito, o se la differenza è maggiore della soglia, ritorna quello con metrica minore      
 return p1_metric < p2_metric ? p1 : p2;  
}
   
static void
update_metric_container(rpl_instance_t *instance)
{
  rpl_path_metric_t path_metric;
  rpl_dag_t *dag;
  //rimeaddr_t macaddr;///FDemicheli
  
  instance->mc.flags = RPL_DAG_MC_FLAG_P;
  instance->mc.aggr = RPL_DAG_MC_AGGR_ADDITIVE;
  instance->mc.prec = 0; //indica che l'oggetto metrica ha la precedenza
  
  instance->mc.node_cycle_time = contikimac_get_cycle_time_for_routing();///node cycle time
  
  dag = instance->current_dag;
    
  if (!dag->joined) {
    /* We should probably do something here */
    return;
  }
 
  if(dag->rank == ROOT_RANK(instance)) {
    path_metric = 0; ///anche x la mia metrica deve essere cosi, x cui lo lascio inalterato
  } else {
    path_metric = calculate_path_metric(dag->preferred_parent);//viene aggiornata la metrica di cammino in base al nuovo preferred parent
   // PRINTF(" path_metric_else = %d\n",path_metric);
  }
  
#if RPL_DAG_MC == RPL_DAG_MC_ETX /*viene def. in rpl-conf.h riga 54*/
 
  instance->mc.type = RPL_DAG_MC_ETX;//la metrica è ETX
  
  instance->mc.length = sizeof(instance->mc.obj.etx);
  
  instance->mc.obj.etx = path_metric;
  
  //PRINTF("update mc: metrica aggiornata = %u\n",instance->mc.obj.etx);

//   PRINTF("RPL: My path ETX to the root is %u.%u\n",
// 	instance->mc.obj.etx / RPL_DAG_MC_ETX_DIVISOR,
// 	(instance->mc.obj.etx % RPL_DAG_MC_ETX_DIVISOR * 100) / RPL_DAG_MC_ETX_DIVISOR);

#elif RPL_DAG_MC == RPL_DAG_MC_ENERGY

  instance->mc.type = RPL_DAG_MC_ENERGY;
  instance->mc.length = sizeof(instance->mc.obj.energy);

  if(dag->rank == ROOT_RANK(instance)) { //viene selezionato un nodo alimentato oppure con la batteria
    instance->mc.type = RPL_DAG_MC_ENERGY_TYPE_MAINS; //nodo alimentato
  } else {
    instance->mc.type = RPL_DAG_MC_ENERGY_TYPE_BATTERY; //nodo con la batteria
  }
  instance->mc.obj.energy.flags = instance->mc.type << RPL_DAG_MC_ENERGY_TYPE;
  instance->mc.obj.energy.flags = instance->mc.type;
  instance->mc.obj.energy.energy_est = path_metric;

#elif RPL_DAG_MC == RPL_DAG_MC_AVG_DELAY /*RMonica*/

  instance->mc.type = RPL_DAG_MC_AVG_DELAY;
  instance->mc.length = sizeof(instance->mc.obj.avg_delay_to_sink);
  instance->mc.obj.avg_delay_to_sink = path_metric;

#elif RPL_DAG_MC == RPL_DAG_MC_EN_TOT
/**This metric is based on work by Riccardo Monica, and evaluate the energy consumption of nodes with different cycle times */
  
  instance->mc.type = RPL_DAG_MC_EN_TOT;
  instance->mc.length = sizeof(instance->mc.obj.entot);//2 byte = 16 bit
  
  ///path_metric: è l'energia totale consumata
  instance->mc.obj.entot = path_metric; ///valore che annuncio nel DIO --> ho detto che invio l'energia consumata totale
  
  
 
  //uip_ds6_get_addr_iid(&(instance->current_dag->preferred_parent->addr),(uip_lladdr_t *)&macaddr);/// Ottengo il PP del nodo corrente
  //instance->mc.pref_parent = (int)(macaddr.u8[7]);
    //PRINTF("PROVA = %u\n",(int)(macaddr.u8[7]));
  
  //PRINTF("en cons da mettere nel DIO = %d\n", path_metric); //è 0 se all_transmit o all_listen sono < 32768

#else

#error "Unsupported RPL_DAG_MC configured. See rpl.h."

#endif /* RPL_DAG_MC */
}

