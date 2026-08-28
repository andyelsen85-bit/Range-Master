CREATE TYPE "public"."product_category" AS ENUM('GAME_CREDIT', 'AMMO_CAL12', 'AMMO_CAL20', 'FOOD', 'DRINK');--> statement-breakpoint
CREATE TABLE "products" (
  "id" serial PRIMARY KEY NOT NULL,
  "code" text,
  "name" text NOT NULL,
  "category" "product_category" NOT NULL,
  "is_system" boolean DEFAULT false NOT NULL,
  "active" boolean DEFAULT true NOT NULL,
  "created_at" timestamp with time zone DEFAULT now() NOT NULL,
  "updated_at" timestamp with time zone DEFAULT now() NOT NULL,
  CONSTRAINT "products_code_unique" UNIQUE("code")
);--> statement-breakpoint
CREATE TABLE "product_price_revisions" (
  "id" serial PRIMARY KEY NOT NULL,
  "product_id" integer NOT NULL,
  "unit_price_cents" integer NOT NULL,
  "effective_from" timestamp with time zone DEFAULT now() NOT NULL,
  "created_at" timestamp with time zone DEFAULT now() NOT NULL
);--> statement-breakpoint
CREATE TABLE "sale_events" (
  "id" serial PRIMARY KEY NOT NULL,
  "external_id" text NOT NULL,
  "spieler_id" integer NOT NULL,
  "datum" date NOT NULL,
  "product_id" integer NOT NULL,
  "price_revision_id" integer NOT NULL,
  "unit_price_cents" integer NOT NULL,
  "quantity" integer NOT NULL,
  "created_at" timestamp with time zone DEFAULT now() NOT NULL,
  CONSTRAINT "sale_events_external_id_unique" UNIQUE("external_id")
);--> statement-breakpoint
ALTER TABLE "product_price_revisions" ADD CONSTRAINT "product_price_revisions_product_id_products_id_fk" FOREIGN KEY ("product_id") REFERENCES "public"."products"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "sale_events" ADD CONSTRAINT "sale_events_spieler_id_spieler_id_fk" FOREIGN KEY ("spieler_id") REFERENCES "public"."spieler"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "sale_events" ADD CONSTRAINT "sale_events_product_id_products_id_fk" FOREIGN KEY ("product_id") REFERENCES "public"."products"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "sale_events" ADD CONSTRAINT "sale_events_price_revision_id_product_price_revisions_id_fk" FOREIGN KEY ("price_revision_id") REFERENCES "public"."product_price_revisions"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
CREATE INDEX "product_price_revisions_product_effective_idx" ON "product_price_revisions" USING btree ("product_id","effective_from");--> statement-breakpoint
CREATE INDEX "sale_events_datum_idx" ON "sale_events" USING btree ("datum");--> statement-breakpoint
CREATE INDEX "sale_events_spieler_datum_idx" ON "sale_events" USING btree ("spieler_id","datum");--> statement-breakpoint
CREATE INDEX "sale_events_product_datum_idx" ON "sale_events" USING btree ("product_id","datum");--> statement-breakpoint